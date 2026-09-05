/* ahk-inputd -- single-instance evdev input broker for AutoHotkey Linux.
 *
 * check_detail0824 §1-C (M4): keyd-style evdev capture -> virtual uinput
 * output -> UNIX-socket client protocol, but instead of being a remap daemon
 * this broker is a MULTI-CLIENT event distributor + suppression arbiter:
 *
 *   - takes EVIOCGRAB on every keyboard device (exclusive capture);
 *   - replays unconsumed events through a virtual keyboard whose vendor ID is
 *     0x0FAC (keyd convention, recognized by libinput quirks);
 *   - a client (each ahk_core script) connects over a UNIX socket and
 *     subscribes to EV_KEY codes with a want_suppress flag; the broker
 *     suppresses replay of a code while ANY live client wants it suppressed;
 *   - every subscribed client receives each matching event frame, so
 *     Hotstring/InputHook/hotkey matchers work without stealing events;
 *   - client disconnect (crash/kill) instantly removes its rules (fail-open);
 *   - SIGALRM watchdog: if the main loop is stuck >2s all grabs are released;
 *   - Backspace->Escape->Enter remains the physical panic escape.
 *
 * Protocol v2 (check0901 P0-3, milestone M3) shares the same socket: frames
 * begin with the "AHK2" magic and carry an explicit LE header (version,
 * header/message length, type, flags, request_id, monotonic client_seq).
 * v2 HELLO negotiates capability grants (OBSERVE/SUPPRESS; EXCLUSIVE/INJECT
 * are M4) and binds a per-process script nonce to a broker-assigned
 * client_id; events carry an authoritative provenance envelope
 * (authority+generation+event_seq, source/confidence, send_level).  v1
 * clients keep the legacy frames and semantics; a v1 u32 payload_len can
 * never equal the magic, so both protocols coexist without length guessing.
 *
 * Build:  cc -O2 -Wall -o ahk-inputd inputd.c   (no X11/AHK dependency)
 * Usage:  ahk-inputd [--socket PATH] [--socket-mode OCTAL]
 * Manual sockets default to 0600; packaged systemd sockets are root:input 0660.
 *         AHK_INPUT_BACKEND=evdev scripts auto-connect to this socket.
 */
#define _GNU_SOURCE /* ppoll */
#include "inputd_proto.h"
#include <linux/input.h>
#include <linux/uinput.h>
#include <sys/ioctl.h>
#include <sys/poll.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <signal.h>
#include <stdarg.h>
#include <fcntl.h>
#include <dirent.h>
#include <errno.h>
#include <unistd.h>
#include <time.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <stdbool.h>
#include <stddef.h>

#define INPUTD_PROTO_VERSION 1u
#define PANIC_TIMEOUT_MS 1500
#define ACTIVATED_IDLE_EXIT_MS 5000
#define MAX_CLIENTS 32
#define MAX_DEVICES 64
#define PANIC_BACKSPACE 14
#define PANIC_ESCAPE 1
#define PANIC_ENTER 28

/* ---- protocol v1 / v2 (see inputd_proto.h) -------------------------------- */

struct rule { unsigned int code; unsigned char suppress; };

struct client {
	int fd;
	pid_t pid;
	uid_t uid;
	gid_t gid;
	unsigned char rx[INPUTD_V2_MAX_FRAME];
	size_t rx_used;
	struct rule rules[INPUTD_MAX_RULES];
	int rule_count;
	char hello_ok;   /* v1 HELLO accepted */
	char dead;
	/* protocol v2 state (check0901 P0-3) */
	char v2;                     /* magic "AHK2" seen on this connection */
	char v2_hello_ok;            /* v2 HELLO accepted (must be first) */
	char rules_confirmed;        /* current generation SUBSCRIBE ACKed */
	unsigned long long v2_client_seq_last;
	unsigned long long client_id;
	unsigned char nonce[16];     /* per-process script nonce, not a PID */
	unsigned int caps_granted;
};

/* ---- little-endian wire helpers ------------------------------------------- */

static void st_le16(unsigned char *p, unsigned int v)
{
	p[0] = (unsigned char)(v & 0xff);
	p[1] = (unsigned char)((v >> 8) & 0xff);
}

static unsigned int ld_le16(const unsigned char *p)
{
	return (unsigned int)p[0] | ((unsigned int)p[1] << 8);
}

static void st_le32(unsigned char *p, unsigned int v)
{
	p[0] = (unsigned char)(v & 0xff);
	p[1] = (unsigned char)((v >> 8) & 0xff);
	p[2] = (unsigned char)((v >> 16) & 0xff);
	p[3] = (unsigned char)((v >> 24) & 0xff);
}

static unsigned int ld_le32(const unsigned char *p)
{
	return (unsigned int)p[0]
		| ((unsigned int)p[1] << 8)
		| ((unsigned int)p[2] << 16)
		| ((unsigned int)p[3] << 24);
}

static void st_le64(unsigned char *p, unsigned long long v)
{
	st_le32(p, (unsigned int)(v & 0xffffffffu));
	st_le32(p + 4, (unsigned int)(v >> 32));
}

static unsigned long long ld_le64(const unsigned char *p)
{
	return (unsigned long long)ld_le32(p)
		| ((unsigned long long)ld_le32(p + 4) << 32);
}

/* ---- state -------------------------------------------------------------- */

static struct client sClients[MAX_CLIENTS];
static int sListenFd = -1;
static int sUinputFd = -1;
static int sDevFds[MAX_DEVICES];
static char sGrabbedNames[MAX_DEVICES][32];
static int sDevCount = 0;
static int sAnyGrabbed = 0;
static int sPanicked = 0;
static int sPanicStage = 0;
static long long sPanicStageMs = 0;
/* check0901 P0-3: broker-side event identity and provenance.  One authority
 * per broker process: a restarted broker gets a fresh authority+generation,
 * so stale event_seq / transaction claims from the previous incarnation are
 * never accepted.  The evdev lane is broker-owned, so its provenance is
 * AUTHORITATIVE (source=PHYSICAL, send_level=-1). */
static unsigned char sAuthorityId[16];
static unsigned long long sGeneration;
static unsigned long long sEventSeq;
static unsigned long long sNextClientId;
static unsigned long long sDevIds[MAX_DEVICES];
/* M2 backend health/generation snapshot. */
static int sProtocolOnly;
static int sReplayErrno;
static unsigned long long sHealthSeq = 1;
static unsigned long long sLastSuccessUs;
static int sHeldStateReconciled;

/* M4 broker-owned injection transactions (check0901 P0-3 §3.3C).  A client
 * opens a transaction, streams key events, then commits/aborts; the broker
 * validates capability, level, quota and TTL, submits the events to its own
 * output device and publishes them on the internal stream with authoritative
 * provenance.  Crash/expiry auto-aborts and balances any still-held keys. */
struct inject_txn {
	struct client *owner;   /* NULL = free slot */
	unsigned long long id;
	int send_level;
	long long deadline_ms;
	unsigned int event_count; /* preflight plan */
	unsigned int events_seen;
	unsigned long long parent;
	unsigned char held[KEY_CNT / 8];
};
static struct inject_txn sTxns[INPUTD_V2_INJECT_MAX_TOTAL];
static long long sLastTtlSweepMs = 0;

/* M4b static arbitration rules (check_detail0901 §7). */
struct arb_rule {
	struct client *owner;  /* NULL = free */
	unsigned long long registration_id;
	unsigned long long acceptance_seq;
	long long lease_expiry_ms;
	unsigned int code;
	unsigned int replacement_code;
	int priority;
	int input_level;
	int replacement_send_level;
	unsigned char mode;
	unsigned char conflict_policy;
	unsigned char dynamic_decision;
	unsigned char dynamic_disabled;
	unsigned int dynamic_timeouts;
	unsigned long long active_replacement_txn;
	unsigned long long active_parent_txn;
	unsigned char replacement_down;
};
static struct arb_rule sArbRules[INPUTD_V2_ARB_MAX_RULES];
static unsigned long long sNextArbAcceptanceSeq;
static unsigned long long sNextBrokerTxnId = 0x8000000000000000ULL;
/* Per-key physical press identity + sticky suppression ownership: if a down
 * was suppressed/remapped, the corresponding up stays suppressed even if the
 * owner crashes or its lease expires before release. */
static unsigned long long sPhysicalTxn[MAX_DEVICES][KEY_CNT];
static unsigned long long sPhysicalOwnerAcceptance[MAX_DEVICES][KEY_CNT];
static unsigned char sPhysicalSuppressed[MAX_DEVICES][KEY_CNT];
struct decision_wait {
	struct arb_rule *rule;
	unsigned long long event_seq;
	unsigned long long source_txn;
	unsigned long long acceptance_seq;
	int result; /* -1 pending, 0 pass, 1 suppress */
};
static struct decision_wait sDecisionWait = {NULL, 0, 0, 0, -1};
/* check0901 P0-1: replay-lane health is separate from the panic path.  Once
 * the replay device fails (create or write), every physical grab is released
 * and the broker becomes observe/listen-only; it never grabs again. */
static int sReplayDead = 0;
static long long sReplayWrites = 0; /* successful replay write batches */
static long sReplayFailAfter = 0;   /* AHK_INPUTD_TEST_REPLAY_FAIL_AFTER */
static int sVerbose = 0;
static volatile sig_atomic_t sQuit = 0;
static volatile sig_atomic_t sWatchdogFired = 0;

static long long now_ms(void)
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (long long)ts.tv_sec * 1000LL + (long long)ts.tv_nsec / 1000000LL;
}

static long long now_us(void)
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (long long)ts.tv_sec * 1000000LL + (long long)ts.tv_nsec / 1000LL;
}

static int write_full(int fd, const void *buf, size_t n); /* fwd */
static void drain_client(struct client *c); /* dynamic decision wait */
static void send_degraded_v2(struct client *c); /* fwd (defined with the v2 writers) */
static void broadcast_backend_health(void); /* M2 fwd */
static void systemd_notify(const char *aState); /* optional NOTIFY_SOCKET */

static void logmsg(const char *fmt, ...)
{
	if (!sVerbose) return;
	va_list ap; va_start(ap, fmt);
	fprintf(stderr, "[inputd] ");
	vfprintf(stderr, fmt, ap);
	fprintf(stderr, "\n");
	fflush(stderr);
	va_end(ap);
}

/* ---- panic escape ------------------------------------------------------- */

static void release_all_grabs(void)
{
	for (int i = 0; i < sDevCount; ++i)
		if (sDevFds[i] >= 0)
			ioctl(sDevFds[i], EVIOCGRAB, 0);
	sAnyGrabbed = 0;
	fprintf(stderr, "[inputd] grabs released (fail-open)\n");
}

/* check0901 P0-1: replay lane is gone.  Release every physical grab first,
 * then tell all clients the backend is listen-only.  The kernel releases the
 * grabs the instant this function returns, so physical input flows again
 * even if later cleanup fails. */
static void replay_dead(void)
{
	if (sReplayDead)
		return;
	sReplayDead = 1;
	sHeldStateReconciled = 0;
	release_all_grabs();
	/* Stop reading the physical devices entirely: they are ungrabbed now and
	 * their events flow to the real consumers; keeping the fds would just
	 * duplicate (and possibly drop) traffic. */
	for (int i = 0; i < sDevCount; ++i)
		if (sDevFds[i] >= 0)
		{
			close(sDevFds[i]);
			sDevFds[i] = -1;
			sGrabbedNames[i][0] = '\0';
		}
	sDevCount = 0;
	memset(sPhysicalTxn, 0, sizeof(sPhysicalTxn));
	memset(sPhysicalOwnerAcceptance, 0, sizeof(sPhysicalOwnerAcceptance));
	memset(sPhysicalSuppressed, 0, sizeof(sPhysicalSuppressed));
	for (int i = 0; i < MAX_CLIENTS; ++i)
	{
		struct client *cl = &sClients[i];
		if (cl->fd >= 0 && !cl->dead
			&& (cl->hello_ok || (cl->v2 && cl->v2_hello_ok)))
		{
			if (cl->v2 && cl->v2_hello_ok)
				send_degraded_v2(cl);
			else
			{
				unsigned char frame[1];
				frame[0] = INPUTD_S2C_BACKEND_DEGRADED;
				if (write_full(cl->fd, frame, sizeof(frame)) != 0)
					cl->dead = 1;
			}
		}
	}
	broadcast_backend_health();
	fprintf(stderr, "[inputd] replay lane failed: grabs released, "
		"listen-only from now on\n");
	fprintf(stderr, "STATUS=degraded: replay unavailable, grabs released\n");
	systemd_notify("READY=1\nSTATUS=degraded: replay unavailable, grabs released");
}

static int panic_step(unsigned int code, int down)
{
	if (!down) return 0;
	long long now = now_ms();
	if (sPanicStage > 0 && now - sPanicStageMs > PANIC_TIMEOUT_MS)
		sPanicStage = 0;
	switch (sPanicStage) {
	case 0:
		if (code == PANIC_BACKSPACE) { sPanicStage = 1; sPanicStageMs = now; }
		break;
	case 1:
		if (code == PANIC_ESCAPE) sPanicStage = 2;
		else if (code == PANIC_BACKSPACE) sPanicStageMs = now;
		else sPanicStage = 0;
		break;
	case 2:
		sPanicStage = 0;
		if (code == PANIC_ENTER) { sPanicked = 1; release_all_grabs(); return 1; }
		break;
	}
	return 0;
}

/* ---- uinput replay device ----------------------------------------------- */

/* check0901 P0-1: every setup ioctl result is validated; a device whose
 * capability bitmap is incomplete must not be treated as a working replay
 * lane.  AHK_INPUTD_TEST_UINPUT_PATH overrides the device for fault oracles
 * (missing device, EACCES, failing UI_DEV_CREATE). */
static int open_uinput_replay(void)
{
	const char *path = getenv("AHK_INPUTD_TEST_UINPUT_PATH");
	if (!path || !*path)
		path = "/dev/uinput";
	int fd = open(path, O_WRONLY | O_NONBLOCK);
	if (fd < 0) { sReplayErrno = errno; fprintf(stderr, "[inputd] %s: %s\n", path, strerror(errno)); return -1; }
	int fail = 0;
	if (ioctl(fd, UI_SET_EVBIT, EV_KEY) != 0
		|| ioctl(fd, UI_SET_EVBIT, EV_SYN) != 0)
		fail = 1;
	for (int c = 1; c < KEY_MAX && !fail; ++c)
		if (ioctl(fd, UI_SET_KEYBIT, c) != 0)
			fail = 1;
	struct uinput_setup setup;
	memset(&setup, 0, sizeof(setup));
	setup.id.bustype = BUS_USB;
	setup.id.vendor  = 0x0FAC; /* keyd vendor id, libinput quirk friendly */
	setup.id.product = 0xABCD;
	setup.id.version = 1;
	strncpy(setup.name, "ahk-inputd virtual keyboard", UINPUT_MAX_NAME_SIZE);
	if (fail || ioctl(fd, UI_DEV_SETUP, &setup) != 0
		|| ioctl(fd, UI_DEV_CREATE) != 0)
	{
		sReplayErrno = errno ? errno : EIO;
		fprintf(stderr, "[inputd] uinput create %s: %s\n", path, strerror(errno));
		close(fd);
		return -1;
	}
	return fd;
}

/* Write one input_event with bounded retry and full-write semantics.
 * EINTR retries; EAGAIN gets a short bounded poll budget; short writes,
 * EPIPE/EIO/ENODEV/EBADF fail immediately (check_detail0901 §1.3-B). */
static int replay_write_event(const struct input_event *e)
{
	for (;;)
	{
		ssize_t r = write(sUinputFd, e, sizeof(*e));
		if (r == (ssize_t)sizeof(*e))
			return 0;
		if (r >= 0)
		{
			fprintf(stderr, "[inputd] replay short write (%zd/%zu): %s\n"
				, r, sizeof(*e), strerror(errno));
			return -1;
		}
		if (errno == EINTR)
			continue;
		if (errno == EAGAIN || errno == EWOULDBLOCK)
		{
			struct pollfd pfd;
			pfd.fd = sUinputFd;
			pfd.events = POLLOUT;
			pfd.revents = 0;
			if (poll(&pfd, 1, 20) > 0 && (pfd.revents & POLLOUT))
				continue;
			fprintf(stderr, "[inputd] replay write stalled: %s\n", strerror(errno));
			return -1;
		}
		fprintf(stderr, "[inputd] replay write failed: %s\n", strerror(errno));
		return -1;
	}
}

/* Replay one EV_KEY frame (single SYN_REPORT per original frame boundary).
 * Returns 0 on success, -1 after fail-open (all grabs already released).
 * AHK_INPUTD_TEST_REPLAY_FAIL_AFTER=N forces the N-th batch to fail so
 * oracles can deterministically exercise the runtime-failure path. */
static int replay_key(unsigned int code, int value)
{
	if (sReplayDead || sUinputFd < 0)
		return -1;
	struct input_event e;
	memset(&e, 0, sizeof(e));
	e.type = EV_KEY; e.code = (__u16)code; e.value = (__s32)value;
	if (replay_write_event(&e) != 0)
	{
		sReplayErrno = errno ? errno : EIO;
		replay_dead();
		return -1;
	}
	memset(&e, 0, sizeof(e));
	e.type = EV_SYN; e.code = SYN_REPORT;
	if (replay_write_event(&e) != 0)
	{
		sReplayErrno = errno ? errno : EIO;
		replay_dead();
		return -1;
	}
	++sReplayWrites;
	sLastSuccessUs = (unsigned long long)now_us();
	sReplayErrno = 0;
	if (sReplayFailAfter > 0 && sReplayWrites >= sReplayFailAfter)
	{
		fprintf(stderr, "[inputd] test hook: forcing replay failure after %lld batch(es)\n"
			, sReplayWrites);
		sReplayErrno = EIO;
		replay_dead();
		return -1;
	}
	return 0;
}

/* True when any key (including modifiers) is currently reported down. */
static void clear_device_physical_state(int device_index)
{
	if (device_index < 0 || device_index >= MAX_DEVICES)
		return;
	memset(sPhysicalTxn[device_index], 0, sizeof(sPhysicalTxn[device_index]));
	memset(sPhysicalOwnerAcceptance[device_index], 0,
		sizeof(sPhysicalOwnerAcceptance[device_index]));
	memset(sPhysicalSuppressed[device_index], 0,
		sizeof(sPhysicalSuppressed[device_index]));
}

static int device_has_held_keys(int fd)
{
	unsigned char keys[KEY_CNT / 8];
	memset(keys, 0, sizeof(keys));
	if (ioctl(fd, EVIOCGKEY(sizeof(keys)), keys) < 0)
		return -1; /* ambiguous: treat as held so the caller defers. */
	for (size_t i = 0; i < sizeof(keys); ++i)
		if (keys[i])
			return 1;
	return 0;
}

/* ---- frame IO ------------------------------------------------------------ */

static int write_full(int fd, const void *buf, size_t n)
{
	size_t sent = 0;
	while (sent < n)
	{
		ssize_t r = write(fd, (const char *)buf + sent, n - sent);
		if (r < 0) { if (errno == EINTR) continue; return -1; }
		sent += (size_t)r;
	}
	return 0;
}

static void send_event_to(struct client *c, unsigned int code, int value, long long ts)
{
	if (c->dead) return;
	unsigned char frame[1 + 4 + 1 + 8];
	frame[0] = INPUTD_S2C_EVENT;
	memcpy(frame + 1, &code, 4);
	frame[5] = (unsigned char)value;
	memcpy(frame + 6, &ts, 8);
	if (write_full(c->fd, frame, sizeof(frame)) != 0)
		c->dead = 1;
}

static void send_ack(struct client *c, unsigned char ok, unsigned int detail)
{
	unsigned char frame[1 + 1 + 4];
	frame[0] = INPUTD_S2C_ACK; frame[1] = ok;
	memcpy(frame + 2, &detail, 4);
	if (write_full(c->fd, frame, sizeof(frame)) != 0)
		c->dead = 1;
}

static void send_pong(struct client *c)
{
	unsigned char frame[1];
	frame[0] = INPUTD_S2C_PONG;
	if (write_full(c->fd, frame, sizeof(frame)) != 0)
		c->dead = 1;
}

/* ---- protocol v2 frame writers -------------------------------------------- */

/* One v2 frame = magic + header + payload.  Every multi-byte field is
 * serialized little-endian; no C struct is ever written raw (check_detail0901
 * §3.2).  request_id/client_seq are 0 on unsolicited broker frames. */
static int send_frame_v2(struct client *c, unsigned int mtype, unsigned int flags
	, const unsigned char *payload, size_t payload_len)
{
	if (c->dead)
		return -1;
	unsigned char frame[INPUTD_V2_MAX_FRAME];
	if (4 + INPUTD_V2_HEADER_LEN + payload_len > sizeof(frame))
		return -1;
	unsigned char *p = frame;
	st_le32(p, INPUTD_V2_MAGIC); p += 4;
	st_le16(p, INPUTD_V2_PROTO_VERSION); p += 2;
	st_le16(p, INPUTD_V2_HEADER_LEN); p += 2;
	st_le32(p, INPUTD_V2_HEADER_LEN + (unsigned int)payload_len); p += 4;
	st_le16(p, mtype); p += 2;
	st_le16(p, flags); p += 2;
	st_le64(p, 0); p += 8; /* request_id */
	st_le64(p, 0); p += 8; /* client_seq (S2C frames carry none) */
	if (payload_len)
		memcpy(p, payload, payload_len);
	if (write_full(c->fd, frame, 4 + INPUTD_V2_HEADER_LEN + payload_len) != 0)
	{
		c->dead = 1;
		return -1;
	}
	return 0;
}

static void send_error_v2(struct client *c, unsigned int code, const char *detail)
{
	size_t dlen = detail ? strlen(detail) : 0;
	if (dlen > 96)
		dlen = 96;
	unsigned char payload[4 + 2 + 96];
	st_le32(payload, code);
	st_le16(payload + 4, (unsigned int)dlen);
	if (dlen)
		memcpy(payload + 6, detail, dlen);
	send_frame_v2(c, INPUTD_V2_ERROR, 0, payload, 6 + dlen);
}

static void send_hello_ack_v2(struct client *c, unsigned int caps_granted
	, unsigned int caps_denied)
{
	unsigned char payload[INPUTD_V2_HELLO_ACK_PAYLOAD_LEN];
	memset(payload, 0, sizeof(payload));
	st_le16(payload, INPUTD_V2_PROTO_VERSION);
	st_le64(payload + 2, c->client_id);
	memcpy(payload + 10, sAuthorityId, 16);
	st_le64(payload + 26, sGeneration);
	st_le32(payload + 34, caps_granted);
	st_le32(payload + 38, caps_denied);
	st_le64(payload + 42, sEventSeq);
	st_le16(payload + 50, sReplayDead ? INPUTD_V2_ACK_FLAG_DEGRADED : 0);
	send_frame_v2(c, INPUTD_V2_HELLO_ACK, 0, payload, sizeof(payload));
}

static void send_event_v2(struct client *c, unsigned int code, int value
	, long long ts, unsigned long long device_id
	, unsigned long long producer, unsigned long long txn
	, unsigned long long parent, int send_level, unsigned char source)
{
	unsigned char payload[INPUTD_V2_ENVELOPE_LEN + INPUTD_V2_KEY_PAYLOAD_LEN];
	unsigned char *p = payload;
	memcpy(p, sAuthorityId, 16); p += 16;
	st_le64(p, sGeneration); p += 8;
	st_le64(p, sEventSeq); p += 8;
	st_le64(p, (unsigned long long)ts); p += 8;
	st_le64(p, device_id); p += 8;
	*p++ = source;
	*p++ = source == INPUTD_V2_SOURCE_PHYSICAL
		? INPUTD_V2_ORIGIN_EVDEV : INPUTD_V2_ORIGIN_UINPUT; /* broker-owned output */
	*p++ = INPUTD_V2_CONF_AUTHORITATIVE;
	*p++ = 0;                          /* reserved */
	st_le16(p, (unsigned int)(int)send_level); p += 2;
	st_le16(p, INPUTD_V2_PAYLOAD_KEY); p += 2;
	st_le16(p, INPUTD_V2_KEY_PAYLOAD_LEN); p += 2;
	st_le64(p, producer); p += 8;
	st_le64(p, txn); p += 8;
	st_le64(p, parent); p += 8;
	st_le32(p, code); p += 4;
	st_le16(p, 0); p += 2;  /* vk: not mapped by the broker */
	st_le16(p, 0); p += 2;  /* sc: not mapped by the broker */
	st_le32(p, 0); p += 4;  /* unicode_scalar */
	*p++ = value == 2 ? INPUTD_V2_PHASE_REPEAT
		: (value ? INPUTD_V2_PHASE_DOWN : INPUTD_V2_PHASE_UP);
	*p++ = (unsigned char)value;
	st_le16(p, 0); p += 2;  /* reserved */
	send_frame_v2(c, INPUTD_V2_EVENT, 0, payload, sizeof(payload));
}

static void send_degraded_v2(struct client *c)
{
	send_frame_v2(c, INPUTD_V2_BACKEND_DEGRADED, 0, NULL, 0);
}

static unsigned health_grabbed_count(void)
{
	unsigned count = 0;
	for (int i = 0; i < sDevCount; ++i)
		if (sDevFds[i] >= 0) ++count;
	return count;
}

static unsigned health_registration_count(void)
{
	unsigned count = 0;
	for (int i = 0; i < MAX_CLIENTS; ++i)
		if (sClients[i].fd >= 0) count += (unsigned)sClients[i].rule_count;
	for (int i = 0; i < INPUTD_V2_ARB_MAX_RULES; ++i)
		if (sArbRules[i].owner) ++count;
	return count;
}

static unsigned health_transaction_count(void)
{
	unsigned count = 0;
	for (int i = 0; i < INPUTD_V2_INJECT_MAX_TOTAL; ++i)
		if (sTxns[i].owner) ++count;
	return count;
}

static void send_backend_health(struct client *c)
{
	if (!c || c->fd < 0 || c->dead || !c->v2 || !c->v2_hello_ok)
		return;
	unsigned char state;
	unsigned char permission;
	unsigned int flags = INPUTD_V2_HEALTH_FLAG_AUTHORITATIVE;
	if (sHeldStateReconciled)
		flags |= INPUTD_V2_HEALTH_FLAG_HELD_STATE_RECONCILED;
	const char *reason;
	if (sProtocolOnly)
	{
		state = INPUTD_V2_HEALTH_AVAILABLE;
		permission = INPUTD_V2_PERMISSION_UNKNOWN;
		reason = "protocol-only: output disabled";
	}
	else if (sReplayDead || sUinputFd < 0)
	{
		state = INPUTD_V2_HEALTH_DEGRADED;
		permission = sReplayErrno == EACCES || sReplayErrno == EPERM
			? INPUTD_V2_PERMISSION_DENIED : INPUTD_V2_PERMISSION_UNKNOWN;
		reason = "replay unavailable; grabs released";
	}
	else
	{
		int coverage_missing = c->rules_confirmed && c->rule_count > 0
			&& sDevCount == 0;
		state = coverage_missing ? INPUTD_V2_HEALTH_DEGRADED
			: (c->rules_confirmed && sHeldStateReconciled
				? INPUTD_V2_HEALTH_HEALTHY
				: (c->rules_confirmed ? INPUTD_V2_HEALTH_RECONCILING_STATE
					: INPUTD_V2_HEALTH_BINDING));
		permission = INPUTD_V2_PERMISSION_GRANTED;
		flags |= INPUTD_V2_HEALTH_FLAG_REPLAY_AVAILABLE;
		reason = !c->rules_confirmed
			? "hello acknowledged; registration reconciliation pending"
			: (coverage_missing ? "registrations ACKed; no keyboard device coverage"
				: (sHeldStateReconciled ? "healthy; registrations and held state reconciled"
					: "registrations ACKed; held-state/device reconciliation pending"));
	}
	if (c->rules_confirmed)
		flags |= INPUTD_V2_HEALTH_FLAG_REGISTRATIONS_RECONCILED;
	size_t rlen = strlen(reason);
	if (rlen > 160) rlen = 160;
	unsigned char payload[INPUTD_V2_HEALTH_BASE_LEN + 160];
	memset(payload, 0, sizeof(payload));
	payload[0] = state;
	payload[1] = permission;
	st_le16(payload + 2, flags);
	st_le64(payload + 4, sGeneration);
	st_le64(payload + 12, sHealthSeq);
	st_le64(payload + 20, sLastSuccessUs);
	st_le32(payload + 28, (unsigned int)sReplayErrno);
	st_le32(payload + 32, (unsigned int)sDevCount);
	st_le32(payload + 36, health_grabbed_count());
	st_le32(payload + 40, health_registration_count());
	st_le32(payload + 44, health_transaction_count());
	st_le16(payload + 48, (unsigned int)rlen);
	memcpy(payload + INPUTD_V2_HEALTH_BASE_LEN, reason, rlen);
	send_frame_v2(c, INPUTD_V2_BACKEND_HEALTH, 0, payload,
		INPUTD_V2_HEALTH_BASE_LEN + rlen);
}

static void broadcast_backend_health(void)
{
	++sHealthSeq;
	for (int i = 0; i < MAX_CLIENTS; ++i)
		send_backend_health(&sClients[i]);
}

static void send_device_added_v2(struct client *c, unsigned long long device_id
	, const char *name)
{
	size_t nlen = strlen(name);
	if (nlen > 255)
		nlen = 255;
	unsigned char payload[8 + 2 + 256];
	st_le64(payload, device_id);
	st_le16(payload + 8, (unsigned int)nlen);
	memcpy(payload + 10, name, nlen);
	send_frame_v2(c, INPUTD_V2_DEVICE_ADDED, 0, payload, 10 + nlen);
}

static void send_device_removed_v2(struct client *c, unsigned long long device_id)
{
	unsigned char payload[8];
	st_le64(payload, device_id);
	send_frame_v2(c, INPUTD_V2_DEVICE_REMOVED, 0, payload, sizeof(payload));
}

static void broadcast_device_added(unsigned long long device_id, const char *name)
{
	for (int i = 0; i < MAX_CLIENTS; ++i)
	{
		struct client *cl = &sClients[i];
		if (cl->fd >= 0 && !cl->dead && cl->v2 && cl->v2_hello_ok)
			send_device_added_v2(cl, device_id, name);
	}
	broadcast_backend_health();
}

static void broadcast_device_removed(unsigned long long device_id)
{
	for (int i = 0; i < MAX_CLIENTS; ++i)
	{
		struct client *cl = &sClients[i];
		if (cl->fd >= 0 && !cl->dead && cl->v2 && cl->v2_hello_ok)
			send_device_removed_v2(cl, device_id);
	}
	broadcast_backend_health();
}

/* ---- M4 broker-owned injection -------------------------------------------- */

static void send_inject_ack(struct client *c, unsigned long long txn
	, unsigned char status, const char *detail)
{
	size_t dlen = detail ? strlen(detail) : 0;
	if (dlen > 96)
		dlen = 96;
	unsigned char payload[8 + 1 + 2 + 96];
	st_le64(payload, txn);
	payload[8] = status;
	st_le16(payload + 9, (unsigned int)dlen);
	if (dlen)
		memcpy(payload + 11, detail, dlen);
	send_frame_v2(c, INPUTD_V2_INJECT_ACK, 0, payload, 11 + dlen);
}

static struct inject_txn *find_txn(struct client *c, unsigned long long id)
{
	for (int i = 0; i < INPUTD_V2_INJECT_MAX_TOTAL; ++i)
		if (sTxns[i].owner == c && sTxns[i].id == id)
			return &sTxns[i];
	return NULL;
}

static int client_txn_count(struct client *c)
{
	int count = 0;
	for (int i = 0; i < INPUTD_V2_INJECT_MAX_TOTAL; ++i)
		if (sTxns[i].owner == c)
			++count;
	return count;
}

static struct inject_txn *alloc_txn(struct client *c, unsigned long long id
	, int send_level, unsigned int ttl_ms, unsigned int event_count
	, unsigned long long parent)
{
	for (int i = 0; i < INPUTD_V2_INJECT_MAX_TOTAL; ++i)
		if (!sTxns[i].owner)
		{
			struct inject_txn *t = &sTxns[i];
			memset(t, 0, sizeof(*t));
			t->owner = c;
			t->id = id;
			t->send_level = send_level;
			t->deadline_ms = now_ms()
				+ (ttl_ms ? (long long)ttl_ms : INPUTD_V2_INJECT_DEFAULT_TTL_MS);
			t->event_count = event_count;
			t->parent = parent;
			return t;
		}
	return NULL;
}

static void inject_publish(struct client *producer, struct inject_txn *t
	, unsigned int code, int value)
{
	/* One acceptance stamp per synthetic event; every recipient sees the
	 * same authority/generation/seq + transaction provenance.  v1 clients
	 * receive the legacy 14-byte frame (provenance-free, observe-only). */
	++sEventSeq;
	long long ts = now_us();
	for (int i = 0; i < MAX_CLIENTS; ++i)
	{
		struct client *cl = &sClients[i];
		if (cl->fd < 0 || cl->dead || !cl->hello_ok
			&& !(cl->v2 && cl->v2_hello_ok))
			continue;
		int subscribed = 0;
		for (int r = 0; r < cl->rule_count; ++r)
			if (cl->rules[r].code == code)
			{
				subscribed = 1;
				break;
			}
		if (!subscribed)
			continue;
		if (cl->v2 && cl->v2_hello_ok)
			send_event_v2(cl, code, value, ts, 0, producer->client_id, t->id,
				t->parent, t->send_level,
				cl == producer ? INPUTD_V2_SOURCE_SELF : INPUTD_V2_SOURCE_OTHER);
		else
			send_event_to(cl, code, value, ts);
	}
}

/* Release every still-held key of a transaction: write the key-up to the
 * output device and publish the synthetic release so no observer (and no
 * desktop) is left with a stuck key.  Call with the txns already marked. */
static void txn_balance_and_close(struct inject_txn *t)
{
	struct client *owner = t->owner;
	for (unsigned int code = 1; code < KEY_CNT; ++code)
		if (t->held[code / 8] & (1u << (code & 7)))
		{
			/* replay_key() fails open (replay_dead) when the lane is gone. */
			(void)replay_key(code, 0);
			if (owner && owner->fd >= 0 && !owner->dead)
				inject_publish(owner, t, code, 0);
		}
	t->owner = NULL;
}

static void abort_client_txns(struct client *c)
{
	for (int i = 0; i < INPUTD_V2_INJECT_MAX_TOTAL; ++i)
	{
		struct inject_txn *t = &sTxns[i];
		if (t->owner == c)
		{
			fprintf(stderr, "[inputd] inject txn %llu client %llu aborted "
				"(owner gone); balancing %s\n", t->id, c->client_id,
				"held keys");
			txn_balance_and_close(t);
		}
	}
}

static void sweep_inject_ttl(void)
{
	long long now = now_ms();
	for (int i = 0; i < INPUTD_V2_INJECT_MAX_TOTAL; ++i)
	{
		struct inject_txn *t = &sTxns[i];
		if (t->owner && now >= t->deadline_ms)
		{
			fprintf(stderr, "[inputd] inject txn %llu client %llu timed out; "
				"aborting and balancing\n", t->id, t->owner->client_id);
			if (t->owner->fd >= 0 && !t->owner->dead)
				send_inject_ack(t->owner, t->id, INPUTD_V2_INJECT_STALE,
					"transaction timed out");
			txn_balance_and_close(t);
		}
	}
}

/* ---- M4b static arbitration ----------------------------------------------- */

static void send_arb_ack(struct client *c, unsigned long long reg_id
	, unsigned char status, unsigned long long owner_id
	, unsigned long long acceptance_seq, unsigned long long lease_expiry_ms)
{
	unsigned char payload[INPUTD_V2_ARB_REGISTER_ACK_PAYLOAD_LEN];
	st_le64(payload, reg_id);
	payload[8] = status;
	st_le64(payload + 9, owner_id);
	st_le64(payload + 17, acceptance_seq);
	st_le64(payload + 25, lease_expiry_ms);
	send_frame_v2(c, INPUTD_V2_ARB_REGISTER_ACK, 0, payload, sizeof(payload));
}

static void send_conflict(struct client *c, unsigned long long requested_id
	, unsigned long long owner_id, unsigned long long owner_client_id
	, unsigned int code, unsigned char reason
	, int owner_priority, int requester_priority)
{
	if (!c || c->fd < 0 || c->dead || !c->v2_hello_ok)
		return;
	unsigned char payload[INPUTD_V2_CONFLICT_PAYLOAD_LEN];
	st_le64(payload, requested_id);
	st_le64(payload + 8, owner_id);
	st_le64(payload + 16, owner_client_id);
	st_le32(payload + 24, code);
	payload[28] = reason;
	st_le16(payload + 29, (unsigned int)(unsigned short)owner_priority);
	st_le16(payload + 31, (unsigned int)(unsigned short)requester_priority);
	send_frame_v2(c, INPUTD_V2_CONFLICT, 0, payload, sizeof(payload));
}

static void broadcast_decision(unsigned long long event_seq
	, unsigned long long source_txn, unsigned int code, unsigned char action
	, unsigned char reason, unsigned long long winner_reg
	, unsigned long long replacement_txn, int priority)
{
	unsigned char payload[INPUTD_V2_ARB_DECISION_PAYLOAD_LEN];
	st_le64(payload, event_seq);
	st_le64(payload + 8, source_txn);
	st_le32(payload + 16, code);
	payload[20] = action;
	payload[21] = reason;
	st_le64(payload + 22, winner_reg);
	st_le64(payload + 30, replacement_txn);
	st_le16(payload + 38, (unsigned int)(unsigned short)priority);
	for (int i = 0; i < MAX_CLIENTS; ++i)
	{
		struct client *cl = &sClients[i];
		if (cl->fd >= 0 && !cl->dead && cl->v2 && cl->v2_hello_ok)
			send_frame_v2(cl, INPUTD_V2_ARB_DECISION, 0, payload, sizeof(payload));
	}
}

static struct arb_rule *find_arb_rule(struct client *c, unsigned long long id)
{
	for (int i = 0; i < INPUTD_V2_ARB_MAX_RULES; ++i)
		if (sArbRules[i].owner == c && sArbRules[i].registration_id == id)
			return &sArbRules[i];
	return NULL;
}

static int arb_client_count(struct client *c)
{
	int count = 0;
	for (int i = 0; i < INPUTD_V2_ARB_MAX_RULES; ++i)
		if (sArbRules[i].owner == c)
			++count;
	return count;
}

static int arb_better(const struct arb_rule *a, const struct arb_rule *b)
{
	if (!b)
		return 1;
	if (a->priority != b->priority)
		return a->priority > b->priority;
	return a->acceptance_seq < b->acceptance_seq;
}

/* EXCLUSIVE/REMAP outrank ordinary SUPPRESS.  OBSERVE never changes delivery. */
static struct arb_rule *arb_by_acceptance(unsigned long long acceptance_seq)
{
	if (!acceptance_seq)
		return NULL;
	for (int i = 0; i < INPUTD_V2_ARB_MAX_RULES; ++i)
		if (sArbRules[i].owner && sArbRules[i].acceptance_seq == acceptance_seq)
			return &sArbRules[i];
	return NULL;
}

static struct arb_rule *arb_winner_filtered(unsigned int code,
	const unsigned long long *excluded, int excluded_count)
{
	struct arb_rule *exclusive = NULL;
	struct arb_rule *suppress = NULL;
	for (int i = 0; i < INPUTD_V2_ARB_MAX_RULES; ++i)
	{
		struct arb_rule *r = &sArbRules[i];
		if (!r->owner || r->code != code || r->dynamic_disabled)
			continue;
		int skip = 0;
		for (int e = 0; e < excluded_count; ++e)
			if (r->acceptance_seq == excluded[e]) { skip = 1; break; }
		if (skip) continue;
		if (r->mode == INPUTD_V2_ARB_EXCLUSIVE || r->mode == INPUTD_V2_ARB_REMAP)
		{
			if (arb_better(r, exclusive)) exclusive = r;
		}
		else if (r->mode == INPUTD_V2_ARB_SUPPRESS)
		{
			if (arb_better(r, suppress)) suppress = r;
		}
	}
	return exclusive ? exclusive : suppress;
}

static struct arb_rule *arb_winner(unsigned int code)
{
	return arb_winner_filtered(code, NULL, 0);
}

static void arb_neutralize(struct arb_rule *r)
{
	if (!r || !r->owner || !r->replacement_down || !r->replacement_code)
		return;
	struct inject_txn synthetic;
	memset(&synthetic, 0, sizeof(synthetic));
	synthetic.owner = r->owner;
	synthetic.id = r->active_replacement_txn;
	synthetic.parent = r->active_parent_txn;
	synthetic.send_level = r->replacement_send_level;
	(void)replay_key(r->replacement_code, 0);
	inject_publish(r->owner, &synthetic, r->replacement_code, 0);
	r->replacement_down = 0;
	r->active_replacement_txn = 0;
	r->active_parent_txn = 0;
}

static void arb_remove(struct arb_rule *r)
{
	if (!r || !r->owner)
		return;
	arb_neutralize(r);
	memset(r, 0, sizeof(*r));
}

static void abort_client_arb_rules(struct client *c)
{
	for (int i = 0; i < INPUTD_V2_ARB_MAX_RULES; ++i)
	{
		struct arb_rule *r = &sArbRules[i];
		if (r->owner == c)
		{
			fprintf(stderr, "[inputd] arb registration %llu client %llu released "
				"(owner gone)\n", r->registration_id, c->client_id);
			arb_remove(r);
		}
	}
}

static void sweep_arb_leases(void)
{
	long long now = now_ms();
	for (int i = 0; i < INPUTD_V2_ARB_MAX_RULES; ++i)
	{
		struct arb_rule *r = &sArbRules[i];
		if (r->owner && now >= r->lease_expiry_ms)
		{
			struct client *owner = r->owner;
			unsigned long long id = r->registration_id;
			unsigned int code = r->code;
			int priority = r->priority;
			fprintf(stderr, "[inputd] arb registration %llu client %llu lease expired\n",
				id, owner->client_id);
			send_arb_ack(owner, id, INPUTD_V2_ARB_EXPIRED, 0,
				r->acceptance_seq, (unsigned long long)r->lease_expiry_ms);
			send_conflict(owner, id, 0, 0, code, INPUTD_V2_CONFLICT_LEASE_EXPIRED,
				0, priority);
			arb_remove(r);
		}
	}
}

/* ---- protocol v2 command handling ------------------------------------------ */

static int client_can_privileged_input(const struct client *c)
{
	/* A per-user daemon may grant its own UID; the packaged root daemon must
	 * not turn root:input socket membership into suppress/exclusive authority. */
	return c && (c->uid == 0 || c->uid == geteuid());
}

static void handle_client_cmd_v2(struct client *c, unsigned int mtype
	, const unsigned char *payload, size_t n, unsigned long long client_seq)
{
	if (c->v2_hello_ok)
	{
		if (mtype == INPUTD_V2_HELLO)
		{
			send_error_v2(c, INPUTD_V2_ERR_DUPLICATE_HELLO, "HELLO already accepted");
			c->dead = 1;
			return;
		}
		/* Monotonic client sequence: duplicates/replays are rejected. */
		if (client_seq <= c->v2_client_seq_last)
		{
			send_error_v2(c, INPUTD_V2_ERR_SEQUENCE_VIOLATION, "client_seq must increase");
			c->dead = 1;
			return;
		}
	}
	else if (mtype != INPUTD_V2_HELLO)
	{
		send_error_v2(c, INPUTD_V2_ERR_NOT_HELLOED, "HELLO must be the first frame");
		c->dead = 1;
		return;
	}
	c->v2_client_seq_last = client_seq;

	switch (mtype)
	{
	case INPUTD_V2_HELLO:
	{
		if (n != INPUTD_V2_HELLO_PAYLOAD_LEN)
		{
			send_error_v2(c, INPUTD_V2_ERR_BAD_FRAME, "bad HELLO payload length");
			c->dead = 1;
			return;
		}
		unsigned int min_proto = ld_le16(payload);
		unsigned int max_proto = ld_le16(payload + 2);
		if (max_proto < INPUTD_V2_PROTO_VERSION
			|| min_proto > INPUTD_V2_PROTO_VERSION)
		{
			send_error_v2(c, INPUTD_V2_ERR_PROTO_UNSUPPORTED, "protocol range excludes v2");
			c->dead = 1;
			return;
		}
		memcpy(c->nonce, payload + 4, 16);
		unsigned int caps_requested = ld_le32(payload + 20);
		/* Capability grants (check_detail0901 §3.5/§7): OBSERVE for every
		 * socket-authorized client; SUPPRESS/EXCLUSIVE/INJECT only for the
		 * socket owner (or root). REMAP requires both EXCLUSIVE and INJECT.
		 * caps_denied reports every requested-but-not-granted bit. */
		unsigned int caps_granted = INPUTD_V2_CAP_OBSERVE;
		if (client_can_privileged_input(c))
			caps_granted |= INPUTD_V2_CAP_SUPPRESS | INPUTD_V2_CAP_EXCLUSIVE
				| INPUTD_V2_CAP_INJECT;
		unsigned int caps_denied = caps_requested & ~caps_granted;
		c->client_id = ++sNextClientId;
		c->caps_granted = caps_granted;
		c->v2_hello_ok = 1;
		c->rules_confirmed = 0;
		send_hello_ack_v2(c, caps_granted, caps_denied);
		logmsg("client %d v2 hello uid=%ld client_id=%llu caps=0x%x denied=0x%x",
			c->fd, (long)c->uid, c->client_id, caps_granted, caps_denied);
		break;
	}
	case INPUTD_V2_SUBSCRIBE:
	{
		if (n < 4)
		{
			send_error_v2(c, INPUTD_V2_ERR_BAD_FRAME, "bad SUBSCRIBE payload");
			c->dead = 1;
			return;
		}
		unsigned int count = ld_le32(payload);
		if (count > INPUTD_MAX_RULES || n != 4 + (size_t)count * 5)
		{
			send_error_v2(c, INPUTD_V2_ERR_BAD_FRAME, "bad SUBSCRIBE rule count");
			c->dead = 1;
			return;
		}
		for (unsigned int i = 0; i < count; ++i)
		{
			const unsigned char *r = payload + 4 + (size_t)i * 5;
			unsigned int code = ld_le32(r);
			if (!code || code > KEY_MAX || r[4] > 1)
			{
				send_error_v2(c, INPUTD_V2_ERR_BAD_FRAME, "bad SUBSCRIBE rule");
				c->dead = 1;
				return;
			}
			if (r[4] && !(c->caps_granted & INPUTD_V2_CAP_SUPPRESS))
			{
				/* Machine-readable denial: the whole update is rejected and
				 * the previous rule set stays active. */
				send_error_v2(c, INPUTD_V2_ERR_CAPABILITY_DENIED
					, "SUPPRESS capability not granted");
				c->dead = 1;
				return;
			}
		}
		c->rule_count = 0;
		for (unsigned int i = 0; i < count; ++i)
		{
			const unsigned char *r = payload + 4 + (size_t)i * 5;
			c->rules[c->rule_count].code = ld_le32(r);
			c->rules[c->rule_count].suppress = r[4];
			++c->rule_count;
		}
		unsigned char ack[5];
		ack[0] = 1;
		st_le32(ack + 1, (unsigned int)c->rule_count);
		c->rules_confirmed = 1;
		if (c->rule_count)
			logmsg("client %d v2 subscribed %d rule(s), first=%u", c->fd,
				c->rule_count, c->rules[0].code);
		else
			logmsg("client %d v2 subscribed 0 rules", c->fd);
		send_frame_v2(c, INPUTD_V2_SUBSCRIBE_ACK, 0, ack, sizeof(ack));
		broadcast_backend_health();
		break;
	}
	case INPUTD_V2_UNSUBSCRIBE:
	{
		c->rule_count = 0;
		c->rules_confirmed = 1;
		unsigned char ack[1];
		ack[0] = 1;
		send_frame_v2(c, INPUTD_V2_UNSUBSCRIBE_ACK, 0, ack, sizeof(ack));
		broadcast_backend_health();
		break;
	}
	case INPUTD_V2_PING:
		send_frame_v2(c, INPUTD_V2_PONG, 0, NULL, 0);
		++sHealthSeq;
		send_backend_health(c);
		break;
	case INPUTD_V2_DECISION_REPLY:
	{
		if (n != INPUTD_V2_DECISION_REPLY_PAYLOAD_LEN)
		{
			send_error_v2(c, INPUTD_V2_ERR_BAD_FRAME,
				"bad DECISION_REPLY payload");
			break;
		}
		unsigned long long event_seq = ld_le64(payload);
		unsigned long long source_txn = ld_le64(payload + 8);
		unsigned long long reg_id = ld_le64(payload + 16);
		unsigned long long acceptance = ld_le64(payload + 24);
		unsigned int action = payload[32];
		if (action > INPUTD_V2_DYNAMIC_SUPPRESS
			|| !sDecisionWait.rule || sDecisionWait.rule->owner != c
			|| sDecisionWait.event_seq != event_seq
			|| sDecisionWait.source_txn != source_txn
			|| sDecisionWait.rule->registration_id != reg_id
			|| sDecisionWait.acceptance_seq != acceptance)
		{
			send_error_v2(c, INPUTD_V2_ERR_SEQUENCE_VIOLATION,
				"stale or mismatched DECISION_REPLY");
			break;
		}
		sDecisionWait.result = action == INPUTD_V2_DYNAMIC_SUPPRESS ? 1 : 0;
		break;
	}
	case INPUTD_V2_ARB_REGISTER:
	{
		unsigned long long reg_id = n >= 8 ? ld_le64(payload) : 0;
		if (n != INPUTD_V2_ARB_REGISTER_PAYLOAD_LEN)
		{
			send_arb_ack(c, reg_id, INPUTD_V2_ARB_BAD_FRAME, 0, 0, 0);
			break;
		}
		unsigned int code = ld_le32(payload + 8);
		unsigned char mode = payload[12];
		unsigned char conflict_policy = payload[13];
		int priority = (int)(short)ld_le16(payload + 14);
		int input_level = (int)(short)ld_le16(payload + 16);
		int replacement_level = (int)(short)ld_le16(payload + 18);
		unsigned int lease_ms = ld_le32(payload + 20);
		unsigned int replacement_code = ld_le32(payload + 24);
		unsigned int flags = ld_le16(payload + 28);
		if (!reg_id || !code || code > KEY_MAX || mode > INPUTD_V2_ARB_REMAP
			|| conflict_policy > INPUTD_V2_ARB_CONFLICT_PREEMPT_LOWER
			|| input_level < 0 || input_level > 100
			|| replacement_level < 0 || replacement_level > 100
			|| lease_ms > INPUTD_V2_ARB_MAX_LEASE_MS
			|| (flags & ~INPUTD_V2_ARB_FLAG_DYNAMIC_DECISION)
			|| ((flags & INPUTD_V2_ARB_FLAG_DYNAMIC_DECISION)
				&& mode != INPUTD_V2_ARB_SUPPRESS)
			|| (mode == INPUTD_V2_ARB_REMAP
				&& (!replacement_code || replacement_code > KEY_MAX
					|| replacement_code == code))
			|| (mode != INPUTD_V2_ARB_REMAP && replacement_code))
		{
			send_arb_ack(c, reg_id, INPUTD_V2_ARB_BAD_FRAME, 0, 0, 0);
			break;
		}
		unsigned int need = INPUTD_V2_CAP_OBSERVE;
		if (mode == INPUTD_V2_ARB_SUPPRESS)
			need = INPUTD_V2_CAP_SUPPRESS;
		else if (mode == INPUTD_V2_ARB_EXCLUSIVE)
			need = INPUTD_V2_CAP_EXCLUSIVE;
		else if (mode == INPUTD_V2_ARB_REMAP)
			need = INPUTD_V2_CAP_EXCLUSIVE | INPUTD_V2_CAP_INJECT;
		if ((c->caps_granted & need) != need)
		{
			send_arb_ack(c, reg_id, INPUTD_V2_ARB_DENIED, 0, 0, 0);
			break;
		}
		if (!lease_ms)
			lease_ms = INPUTD_V2_ARB_DEFAULT_LEASE_MS;
		struct arb_rule *existing = find_arb_rule(c, reg_id);
		if (existing)
		{
			/* Stable id refreshes only the lease; changing selector/policy
			 * under the same id is rejected to avoid ownership confusion. */
			if (existing->code != code || existing->mode != mode
				|| existing->conflict_policy != conflict_policy
				|| existing->replacement_code != replacement_code
				|| existing->priority != priority
				|| existing->input_level != input_level
				|| existing->replacement_send_level != replacement_level
				|| existing->dynamic_decision
					!= ((flags & INPUTD_V2_ARB_FLAG_DYNAMIC_DECISION) != 0))
			{
				send_arb_ack(c, reg_id, INPUTD_V2_ARB_BAD_FRAME, 0,
					existing->acceptance_seq,
					(unsigned long long)existing->lease_expiry_ms);
				break;
			}
			existing->lease_expiry_ms = now_ms() + lease_ms;
			send_arb_ack(c, reg_id, INPUTD_V2_ARB_REFRESHED, reg_id,
				existing->acceptance_seq,
				(unsigned long long)existing->lease_expiry_ms);
			break;
		}
		if (arb_client_count(c) >= (int)INPUTD_V2_ARB_MAX_PER_CLIENT)
		{
			send_arb_ack(c, reg_id, INPUTD_V2_ARB_QUOTA, 0, 0, 0);
			break;
		}
		if (mode == INPUTD_V2_ARB_EXCLUSIVE || mode == INPUTD_V2_ARB_REMAP)
		{
			struct arb_rule *owner = NULL;
			for (int i = 0; i < INPUTD_V2_ARB_MAX_RULES; ++i)
			{
				struct arb_rule *r = &sArbRules[i];
				if (r->owner && r->code == code
					&& (r->mode == INPUTD_V2_ARB_EXCLUSIVE
						|| r->mode == INPUTD_V2_ARB_REMAP)
					&& arb_better(r, owner))
					owner = r;
			}
			if (owner)
			{
				unsigned char reason = owner->owner->uid == c->uid
					? INPUTD_V2_CONFLICT_OWNER_EXISTS
					: INPUTD_V2_CONFLICT_CROSS_UID;
				if (owner->owner->uid == c->uid
					&& conflict_policy == INPUTD_V2_ARB_CONFLICT_PREEMPT_LOWER
					&& priority > owner->priority)
				{
					struct client *old_client = owner->owner;
					unsigned long long old_id = owner->registration_id;
					int old_priority = owner->priority;
					send_conflict(old_client, old_id, reg_id, c->client_id, code,
						INPUTD_V2_CONFLICT_PREEMPTED, priority, old_priority);
					arb_remove(owner);
				}
				else
				{
					send_arb_ack(c, reg_id, INPUTD_V2_ARB_CONFLICTED,
						owner->registration_id, owner->acceptance_seq,
						(unsigned long long)owner->lease_expiry_ms);
					send_conflict(c, reg_id, owner->registration_id,
						owner->owner->client_id, code, reason, owner->priority, priority);
					break;
				}
			}
		}
		struct arb_rule *slot = NULL;
		for (int i = 0; i < INPUTD_V2_ARB_MAX_RULES; ++i)
			if (!sArbRules[i].owner) { slot = &sArbRules[i]; break; }
		if (!slot)
		{
			send_arb_ack(c, reg_id, INPUTD_V2_ARB_QUOTA, 0, 0, 0);
			break;
		}
		memset(slot, 0, sizeof(*slot));
		slot->owner = c;
		slot->registration_id = reg_id;
		slot->acceptance_seq = ++sNextArbAcceptanceSeq;
		slot->lease_expiry_ms = now_ms() + lease_ms;
		slot->code = code;
		slot->replacement_code = replacement_code;
		slot->priority = priority;
		slot->input_level = input_level;
		slot->replacement_send_level = replacement_level;
		slot->mode = mode;
		slot->conflict_policy = conflict_policy;
		slot->dynamic_decision =
			(flags & INPUTD_V2_ARB_FLAG_DYNAMIC_DECISION) != 0;
		send_arb_ack(c, reg_id, INPUTD_V2_ARB_GRANTED, reg_id,
			slot->acceptance_seq, (unsigned long long)slot->lease_expiry_ms);
		logmsg("client %d arb registered id=%llu code=%u mode=%u dynamic=%u",
			c->fd, reg_id, code, (unsigned)mode,
			(unsigned)slot->dynamic_decision);
		break;
	}
	case INPUTD_V2_ARB_UNREGISTER:
	{
		unsigned long long reg_id = n >= 8 ? ld_le64(payload) : 0;
		if (n != 8)
		{
			send_arb_ack(c, reg_id, INPUTD_V2_ARB_BAD_FRAME, 0, 0, 0);
			break;
		}
		struct arb_rule *r = find_arb_rule(c, reg_id);
		if (!r)
		{
			send_arb_ack(c, reg_id, INPUTD_V2_ARB_BAD_FRAME, 0, 0, 0);
			break;
		}
		unsigned long long acceptance = r->acceptance_seq;
		unsigned long long expiry = (unsigned long long)r->lease_expiry_ms;
		arb_remove(r);
		send_arb_ack(c, reg_id, INPUTD_V2_ARB_UNREGISTERED, 0,
			acceptance, expiry);
		break;
	}
	case INPUTD_V2_INJECT_BEGIN:
	{
		unsigned long long id = n >= 8 ? ld_le64(payload) : 0;
		if (n != INPUTD_V2_INJECT_BEGIN_PAYLOAD_LEN)
		{
			send_inject_ack(c, id, INPUTD_V2_INJECT_BAD_FRAME, "bad INJECT_BEGIN payload");
			break;
		}
		int level = (int)(short)ld_le16(payload + 8);
		unsigned int ttl_ms = ld_le16(payload + 12);
		unsigned int event_count = ld_le32(payload + 14);
		unsigned long long parent = ld_le64(payload + 18);
		if (!(c->caps_granted & INPUTD_V2_CAP_INJECT))
		{
			send_inject_ack(c, id, INPUTD_V2_INJECT_DENIED, "INJECT capability not granted");
			break;
		}
		if (level < 0 || level > 100 || event_count < 1
			|| event_count > INPUTD_V2_INJECT_MAX_EVENTS)
		{
			send_inject_ack(c, id, INPUTD_V2_INJECT_BAD_FRAME, "level/event_count out of range");
			break;
		}
		if (find_txn(c, id))
		{
			send_inject_ack(c, id, INPUTD_V2_INJECT_BAD_FRAME, "duplicate transaction id");
			break;
		}
		if (client_txn_count(c) >= (int)INPUTD_V2_INJECT_MAX_PER_CLIENT
			|| !alloc_txn(c, id, level, ttl_ms, event_count, parent))
		{
			send_inject_ack(c, id, INPUTD_V2_INJECT_QUOTA, "transaction quota exceeded");
			break;
		}
		if (sReplayDead || sUinputFd < 0)
		{
			abort_client_txns(c);
			send_inject_ack(c, id, INPUTD_V2_INJECT_DEGRADED, "replay lane unavailable");
			break;
		}
		send_inject_ack(c, id, INPUTD_V2_INJECT_OK_BEGIN, "transaction opened");
		break;
	}
	case INPUTD_V2_INJECT_EVENT:
	{
		unsigned long long id = n >= 8 ? ld_le64(payload) : 0;
		if (n != 24)
		{
			send_inject_ack(c, id, INPUTD_V2_INJECT_BAD_FRAME, "bad INJECT_EVENT payload");
			break;
		}
		struct inject_txn *t = find_txn(c, id);
		if (!t)
		{
			/* check0901 P0-3: a restarted broker must never accept events for
			 * a previous generation's transaction. */
			send_inject_ack(c, id, INPUTD_V2_INJECT_STALE, "unknown transaction");
			break;
		}
		const unsigned char *k = payload + 8;
		unsigned int code = ld_le32(k);
		unsigned int value = k[13];
		unsigned int phase = k[12];
		if (!code || code > KEY_MAX || value > 2
			|| phase > INPUTD_V2_PHASE_REPEAT
			|| (phase == INPUTD_V2_PHASE_DOWN && value != 1)
			|| (phase == INPUTD_V2_PHASE_UP && value != 0)
			|| (phase == INPUTD_V2_PHASE_REPEAT && value != 2))
		{
			send_inject_ack(c, id, INPUTD_V2_INJECT_BAD_FRAME, "bad key payload");
			txn_balance_and_close(t);
			break;
		}
		if (sReplayDead || sUinputFd < 0)
		{
			send_inject_ack(c, id, INPUTD_V2_INJECT_DEGRADED, "replay lane unavailable");
			txn_balance_and_close(t);
			break;
		}
		if (replay_key(code, (int)value) != 0)
		{
			send_inject_ack(c, id, INPUTD_V2_INJECT_DEGRADED, "replay write failed");
			txn_balance_and_close(t);
			break;
		}
		/* Track held keys so commit/abort/crash can balance the set. */
		if (value)
			t->held[code / 8] |= (unsigned char)(1u << (code & 7));
		else
			t->held[code / 8] &= (unsigned char)~(1u << (code & 7));
		++t->events_seen;
		if (t->events_seen > t->event_count)
		{
			send_inject_ack(c, id, INPUTD_V2_INJECT_BAD_FRAME, "event_count plan exceeded");
			txn_balance_and_close(t);
			break;
		}
		inject_publish(c, t, code, (int)value);
		send_inject_ack(c, id, INPUTD_V2_INJECT_OK_EVENT, "event accepted");
		break;
	}
	case INPUTD_V2_INJECT_COMMIT:
	case INPUTD_V2_INJECT_ABORT:
	{
		unsigned char is_abort = mtype == INPUTD_V2_INJECT_ABORT;
		unsigned long long id = n >= 8 ? ld_le64(payload) : 0;
		if (n != 8)
		{
			send_inject_ack(c, id, INPUTD_V2_INJECT_BAD_FRAME, "bad COMMIT/ABORT payload");
			break;
		}
		struct inject_txn *t = find_txn(c, id);
		if (!t)
		{
			send_inject_ack(c, id, INPUTD_V2_INJECT_STALE, "unknown transaction");
			break;
		}
		/* Commit only closes a complete plan.  A short plan is an explicit
		 * partial abort and is balanced, but never reported as OK_COMMIT. */
		if (!is_abort && t->events_seen != t->event_count)
		{
			txn_balance_and_close(t);
			send_inject_ack(c, id, INPUTD_V2_INJECT_BAD_FRAME,
				"commit event_count incomplete");
			break;
		}
		txn_balance_and_close(t);
		send_inject_ack(c, id, is_abort ? INPUTD_V2_INJECT_OK_ABORT
			: INPUTD_V2_INJECT_OK_COMMIT, "transaction closed");
		break;
	}
	default:
		send_error_v2(c, INPUTD_V2_ERR_BAD_FRAME, "unknown message type");
		c->dead = 1;
		break;
	}
}

/* ---- client handling ----------------------------------------------------- */

static void close_client(struct client *c)
{
	/* M4: a crashed/disconnected owner auto-aborts its open transactions,
	 * balances held keys and releases arbitration ownership/leases. */
	abort_client_txns(c);
	abort_client_arb_rules(c);
	if (c->fd >= 0)
	{
		fprintf(stderr, "[inputd] client pid=%ld uid=%ld disconnected; rules dropped\n",
			(long)c->pid, (long)c->uid);
		close(c->fd);
	}
	c->fd = -1;
	c->pid = 0;
	c->uid = 0;
	c->gid = 0;
	c->rx_used = 0;
	c->rule_count = 0;
	c->hello_ok = 0;
	c->dead = 0;
	c->v2 = 0;
	c->v2_hello_ok = 0;
	c->rules_confirmed = 0;
	c->v2_client_seq_last = 0;
	c->client_id = 0;
	c->caps_granted = 0;
	memset(c->nonce, 0, sizeof(c->nonce));
}

static void systemd_notify(const char *aState)
{
	const char *socket_path = getenv("NOTIFY_SOCKET");
	if (!socket_path || !*socket_path || !aState)
		return;
	int fd = socket(AF_UNIX, SOCK_DGRAM | SOCK_CLOEXEC, 0);
	if (fd < 0)
		return;
	struct sockaddr_un addr;
	memset(&addr, 0, sizeof(addr));
	addr.sun_family = AF_UNIX;
	size_t len = strlen(socket_path);
	if (len >= sizeof(addr.sun_path))
	{
		close(fd);
		return;
	}
	if (socket_path[0] == '@')
	{
		addr.sun_path[0] = '\0';
		memcpy(addr.sun_path + 1, socket_path + 1, len - 1);
	}
	else
		memcpy(addr.sun_path, socket_path, len + 1);
	socklen_t addr_len = (socklen_t)(offsetof(struct sockaddr_un, sun_path) + len + 1);
	if (socket_path[0] == '@')
		addr_len = (socklen_t)(offsetof(struct sockaddr_un, sun_path) + len);
	(void)sendto(fd, aState, strlen(aState), MSG_NOSIGNAL,
		(struct sockaddr *)&addr, addr_len);
	close(fd);
}

static int active_client_count(void)
{
	int count = 0;
	for (int i = 0; i < MAX_CLIENTS; ++i)
		if (sClients[i].fd >= 0 && !sClients[i].dead)
			++count;
	return count;
}

static int accept_client(void)
{
	struct sockaddr_un addr;
	socklen_t len = sizeof(addr);
	int fd = accept(sListenFd, (struct sockaddr *)&addr, &len);
	if (fd < 0) return -1;
	int flags = fcntl(fd, F_GETFL, 0);
	fcntl(fd, F_SETFL, flags | O_NONBLOCK);
	struct ucred cred;
	socklen_t cred_len = sizeof(cred);
	memset(&cred, 0, sizeof(cred));
	if (getsockopt(fd, SOL_SOCKET, SO_PEERCRED, &cred, &cred_len) != 0)
	{
		fprintf(stderr, "[inputd] SO_PEERCRED failed: %s\n", strerror(errno));
		close(fd);
		return -1;
	}
	for (int i = 0; i < MAX_CLIENTS; ++i)
		if (sClients[i].fd < 0)
		{
			sClients[i].fd = fd;
			sClients[i].pid = cred.pid;
			sClients[i].uid = cred.uid;
			sClients[i].gid = cred.gid;
			sClients[i].rx_used = 0;
			sClients[i].rule_count = 0;
			sClients[i].hello_ok = 0;
			sClients[i].rules_confirmed = 0;
			fprintf(stderr, "[inputd] client pid=%ld uid=%ld gid=%ld connected\n",
				(long)cred.pid, (long)cred.uid, (long)cred.gid);
			return fd;
		}
	close(fd); /* too many clients: refuse */
	return -1;
}

static void handle_client_cmd(struct client *c, const unsigned char *cmd, size_t n)
{
	if (n < 1) { c->dead = 1; return; }
	switch (cmd[0])
	{
	case INPUTD_C2S_HELLO:
	{
		if (n != 1 + 4) { c->dead = 1; return; }
		unsigned int ver;
		memcpy(&ver, cmd + 1, 4);
		c->hello_ok = ver == INPUTD_PROTO_VERSION;
		send_ack(c, c->hello_ok ? 1 : 0, INPUTD_PROTO_VERSION);
		if (!c->hello_ok)
			c->dead = 1;
		break;
	}
	case INPUTD_C2S_SUBSCRIBE:
	{
		if (!c->hello_ok) { c->dead = 1; return; }
		if (n < 1 + 4) { c->dead = 1; return; }
		unsigned int count;
		memcpy(&count, cmd + 1, 4);
		if (count > INPUTD_MAX_RULES || n != 1 + 4 + (size_t)count * 5) { c->dead = 1; return; }
		for (unsigned int i = 0; i < count; ++i)
		{
			const unsigned char *r = cmd + 5 + (size_t)i * 5;
			unsigned int code;
			memcpy(&code, r, 4);
			if (!code || code > KEY_MAX || r[4] > 1)
			{
				c->dead = 1;
				return;
			}
		}
		/* v1 has no capability field, but it must not be a privilege
		 * downgrade.  Keep observe-only subscriptions available to the
		 * socket-authorized user and reject any legacy suppress request. */
		for (unsigned int i = 0; i < count; ++i)
			if (cmd[5 + (size_t)i * 5 + 4] && !client_can_privileged_input(c))
			{
				c->rule_count = 0;
				send_ack(c, 0, INPUTD_V1_DETAIL_CAPABILITY_DENIED);
				logmsg("client %d v1 suppress subscription denied by capability policy", c->fd);
				return;
			}
		c->rule_count = 0;
		for (unsigned int i = 0; i < count; ++i)
		{
			const unsigned char *r = cmd + 5 + (size_t)i * 5;
			memcpy(&c->rules[c->rule_count].code, r, 4);
			c->rules[c->rule_count].suppress = r[4];
			c->rule_count++;
		}
		logmsg("client %d subscribed %d rule(s)", c->fd, c->rule_count);
		send_ack(c, 1, (unsigned int)c->rule_count);
		break;
	}
	case INPUTD_C2S_UNSUBSCRIBE:
		if (!c->hello_ok) { c->dead = 1; return; }
		c->rule_count = 0;
		send_ack(c, 1, 0);
		break;
	case INPUTD_C2S_PING:
		if (!c->hello_ok) { c->dead = 1; return; }
		send_pong(c);
		break;
	default:
		c->dead = 1;
	}
}

/* ---- device scanning ------------------------------------------------------ */

static void drain_client_v2(struct client *c); /* fwd */

static void drain_client(struct client *c)
{
	/* A connection already classified as v2 stays on the v2 parser. */
	if (c->v2)
	{
		drain_client_v2(c);
		return;
	}
	/* One authorized client must not monopolize the grab/replay loop by keeping
	 * its stream perpetually readable. Four maximum frames per poll turn is
	 * enough for normal rule updates while preserving fairness. */
	size_t read_budget = INPUTD_V2_MAX_FRAME * 4u;
	while (read_budget)
	{
		if (c->rx_used == sizeof(c->rx))
		{
			close_client(c);
			return;
		}
		size_t room = sizeof(c->rx) - c->rx_used;
		if (room > read_budget)
			room = read_budget;
		ssize_t got = read(c->fd, c->rx + c->rx_used, room);
		if (got == 0)
		{
			close_client(c);
			return;
		}
		if (got < 0)
		{
			if (errno == EINTR)
				continue;
			if (errno == EAGAIN || errno == EWOULDBLOCK)
				return;
			close_client(c);
			return;
		}
		c->rx_used += (size_t)got;
		read_budget -= (size_t)got;
		/* Protocol discrimination (check0901 P0-3): a v2 frame starts with
		 * the "AHK2" magic, which can never be a valid v1 u32 payload_len
		 * (v1 frames are bounded by INPUTD_MAX_FRAME << 0x324B4841). */
		if (!c->v2 && c->rx_used >= 4 && ld_le32(c->rx) == INPUTD_V2_MAGIC)
		{
			c->v2 = 1;
			drain_client_v2(c);
			return;
		}
		for (;;)
		{
			if (c->rx_used < 4)
				break;
			unsigned int len;
			memcpy(&len, c->rx, 4);
			if (len < 1 || len > INPUTD_MAX_FRAME - 4)
			{
				close_client(c);
				return;
			}
			size_t frame_size = 4 + (size_t)len;
			if (c->rx_used < frame_size)
				break;
			handle_client_cmd(c, c->rx + 4, len);
			if (c->fd < 0 || c->dead)
				return;
			c->rx_used -= frame_size;
			if (c->rx_used)
				memmove(c->rx, c->rx + frame_size, c->rx_used);
		}
		/* Edge-trigger independence: continue read until EAGAIN. */
	}
}

/* v2 framing: magic + header + payload, validated field by field.  The rx
 * buffer already holds buffered bytes when called from drain_client, so
 * parse BEFORE reading more. */
static void drain_client_v2(struct client *c)
{
	size_t read_budget = INPUTD_V2_MAX_FRAME * 4u;
	for (;;)
	{
		for (;;)
		{
			if (c->rx_used < 4)
				break;
			if (ld_le32(c->rx) != INPUTD_V2_MAGIC)
			{
				send_error_v2(c, INPUTD_V2_ERR_BAD_FRAME, "bad magic");
				close_client(c);
				return;
			}
			if (c->rx_used < 4 + INPUTD_V2_HEADER_LEN)
				break;
			const unsigned char *h = c->rx + 4;
			unsigned int ver = ld_le16(h);
			unsigned int hlen = ld_le16(h + 2);
			unsigned int mlen = ld_le32(h + 4);
			unsigned int mtype = ld_le16(h + 8);
			unsigned long long client_seq = ld_le64(h + 20); /* request_id@12..19 */
			if (ver != INPUTD_V2_PROTO_VERSION
				|| hlen != INPUTD_V2_HEADER_LEN
				|| mlen < INPUTD_V2_HEADER_LEN
				|| mlen > INPUTD_V2_MAX_FRAME - 4)
			{
				send_error_v2(c, INPUTD_V2_ERR_BAD_FRAME, "bad v2 header");
				close_client(c);
				return;
			}
			size_t frame_size = 4 + (size_t)mlen;
			if (c->rx_used < frame_size)
				break;
			handle_client_cmd_v2(c, mtype, c->rx + 4 + INPUTD_V2_HEADER_LEN
				, (size_t)mlen - INPUTD_V2_HEADER_LEN, client_seq);
			if (c->fd < 0 || c->dead)
				return;
			c->rx_used -= frame_size;
			if (c->rx_used)
				memmove(c->rx, c->rx + frame_size, c->rx_used);
		}
		if (c->fd < 0 || c->dead)
			return;
		if (c->rx_used == sizeof(c->rx))
		{
			close_client(c);
			return;
		}
		size_t room = sizeof(c->rx) - c->rx_used;
		if (room > read_budget)
			room = read_budget;
		ssize_t got = read(c->fd, c->rx + c->rx_used, room);
		if (got == 0)
		{
			close_client(c);
			return;
		}
		if (got < 0)
		{
			if (errno == EINTR)
				continue;
			if (errno == EAGAIN || errno == EWOULDBLOCK)
				return;
			close_client(c);
			return;
		}
		c->rx_used += (size_t)got;
		read_budget -= (size_t)got;
	}
}

static int wait_dynamic_decision(struct arb_rule *rule,
	unsigned long long event_seq, unsigned long long source_txn,
	unsigned int code, unsigned int value, unsigned long long deadline_us,
	unsigned char *reason)
{
	struct client *owner = rule ? rule->owner : NULL;
	if (!rule || !owner || owner->fd < 0 || owner->dead)
	{
		*reason = INPUTD_V2_DECISION_DYNAMIC_DISCONNECT;
		return 0;
	}
	if (!deadline_us)
		deadline_us = (unsigned long long)now_us()
			+ INPUTD_V2_DYNAMIC_DEADLINE_MS * 1000ULL;
	unsigned char payload[INPUTD_V2_DECISION_REQUEST_PAYLOAD_LEN];
	memset(payload, 0, sizeof(payload));
	st_le64(payload, event_seq);
	st_le64(payload + 8, source_txn);
	st_le64(payload + 16, rule->registration_id);
	st_le32(payload + 24, code);
	payload[28] = (unsigned char)value;
	st_le64(payload + 30, deadline_us);
	st_le64(payload + 38, rule->acceptance_seq);
	sDecisionWait.rule = rule;
	sDecisionWait.event_seq = event_seq;
	sDecisionWait.source_txn = source_txn;
	sDecisionWait.acceptance_seq = rule->acceptance_seq;
	sDecisionWait.result = -1;
	if (send_frame_v2(owner, INPUTD_V2_DECISION_REQUEST, 0,
			payload, sizeof(payload)) != 0)
	{
		sDecisionWait.rule = NULL;
		*reason = INPUTD_V2_DECISION_DYNAMIC_DISCONNECT;
		return 0;
	}
	while (sDecisionWait.result < 0)
	{
		long long remain_us = (long long)deadline_us - now_us();
		if (remain_us <= 0) break;
		struct pollfd pfd = { owner->fd, POLLIN | POLLHUP | POLLERR, 0 };
		int timeout_ms = (int)((remain_us + 999) / 1000);
		int pr = poll(&pfd, 1, timeout_ms);
		if (pr < 0 && errno == EINTR) continue;
		if (pr <= 0) break;
		if (pfd.revents & (POLLIN | POLLHUP | POLLERR))
			drain_client(owner);
		if (owner->fd < 0 || owner->dead || rule->owner != owner)
			break;
	}
	int result = sDecisionWait.result;
	sDecisionWait.rule = NULL;
	if (result >= 0)
	{
		rule->dynamic_timeouts = 0;
		*reason = result ? INPUTD_V2_DECISION_DYNAMIC_TRUE
			: INPUTD_V2_DECISION_DYNAMIC_FALSE;
		return result;
	}
	if (rule->owner != owner || owner->fd < 0 || owner->dead)
	{
		*reason = INPUTD_V2_DECISION_DYNAMIC_DISCONNECT;
		return 0; // keyboard default fail-open.
	}
	++rule->dynamic_timeouts;
	*reason = INPUTD_V2_DECISION_DYNAMIC_TIMEOUT;
	if (rule->dynamic_timeouts >= INPUTD_V2_DYNAMIC_SLOW_LIMIT)
	{
		rule->dynamic_disabled = 1;
		*reason = INPUTD_V2_DECISION_DYNAMIC_SLOW_DOWNGRADE;
		send_conflict(owner, rule->registration_id, 0, 0, code,
			INPUTD_V2_CONFLICT_SLOW_CLIENT, 0, rule->priority);
		fprintf(stderr, "[inputd] dynamic registration %llu client %llu "
			"downgraded observe-only after %u timeout(s)\n",
			rule->registration_id, owner->client_id, rule->dynamic_timeouts);
	}
	return 0; // fail-open.
}

static int is_keyboard(int fd)
{
	unsigned char bits[KEY_CNT / 8];
	memset(bits, 0, sizeof(bits));
	if (ioctl(fd, EVIOCGBIT(EV_KEY, sizeof(bits)), bits) < 0)
		return 0;
	/* require at least a letter and Enter: a real keyboard */
	if (!(bits[KEY_A / 8] & (1 << (KEY_A & 7))))
		return 0;
	if (!(bits[KEY_ENTER / 8] & (1 << (KEY_ENTER & 7))))
		return 0;
	return 1;
}

static int is_self_device(int fd)
{
	char name[UINPUT_MAX_NAME_SIZE];
	memset(name, 0, sizeof(name));
	if (ioctl(fd, EVIOCGNAME(sizeof(name) - 1), name) < 0)
		return 0;
	/* Never grab our own replay device: that would loop the stream. */
	return strstr(name, "ahk-inputd") != NULL;
}

static void prune_removed_devices(void);

static unsigned long long stable_device_id(int fd, const char *path)
{
	char identity[1024];
	identity[0] = '\0';
	char resolved_path[PATH_MAX] = { 0 };
	const char *canonical = realpath(path, resolved_path);
	const char *link_dirs[] = { "/dev/input/by-id", "/dev/input/by-path" };
	char stable_link[PATH_MAX] = { 0 };
	for (size_t d = 0; d < sizeof(link_dirs) / sizeof(link_dirs[0]) && !stable_link[0]; ++d)
	{
		DIR *dir = opendir(link_dirs[d]);
		if (!dir)
			continue;
		struct dirent *ent;
		while ((ent = readdir(dir)) != NULL)
		{
			if (ent->d_name[0] == '.')
				continue;
			char link_path[PATH_MAX], link_target[PATH_MAX];
			snprintf(link_path, sizeof(link_path), "%s/%s", link_dirs[d], ent->d_name);
			ssize_t n = readlink(link_path, link_target, sizeof(link_target) - 1);
			if (n < 0)
				continue;
			link_target[n] = '\0';
			char resolved_link[PATH_MAX] = { 0 };
			if (realpath(link_path, resolved_link)
				&& canonical && !strcmp(resolved_link, canonical))
			{
				snprintf(stable_link, sizeof(stable_link), "%s/%s",
					link_dirs[d], ent->d_name);
				break;
			}
		}
		closedir(dir);
	}
	struct input_id id;
	memset(&id, 0, sizeof(id));
	char uniq[128] = { 0 };
	char phys[128] = { 0 };
	char name[UINPUT_MAX_NAME_SIZE] = { 0 };
	(void)ioctl(fd, EVIOCGID, &id);
	(void)ioctl(fd, EVIOCGUNIQ(sizeof(uniq) - 1), uniq);
	(void)ioctl(fd, EVIOCGPHYS(sizeof(phys) - 1), phys);
	(void)ioctl(fd, EVIOCGNAME(sizeof(name) - 1), name);
	snprintf(identity, sizeof(identity), "%s|%s|%s|%s|%u:%u:%u:%u|%s",
		stable_link, uniq, phys, name, id.bustype, id.vendor, id.product,
		id.version, canonical ? canonical : path);
	unsigned long long hash = 1469598103934665603ULL;
	for (const unsigned char *p = (const unsigned char *)identity; *p; ++p)
	{
		hash ^= *p;
		hash *= 1099511628211ULL;
	}
	return hash ? hash : 1ULL;
}

static void scan_devices(void)
{
	if (sPanicked)
		return;
	int held_deferred = 0;
	int held_before_state = sHeldStateReconciled;
	/* check0901 P0-1: never grab while the replay lane is unavailable --
	 * without a working replay path an exclusive grab would swallow input. */
	if (sReplayDead || sUinputFd < 0)
	{
		if (!sReplayDead)
			fprintf(stderr, "[inputd] replay not ready; deferring grabs\n");
		return;
	}
	/* Test oracle hook: limit grabs to specific devices (uinput fixtures),
	 * so fault/protocol tests never touch the host keyboard.  Accepts a
	 * comma-separated list of node names or paths (check0901 P0-3 oracles
	 * hot-add a second fixture while the broker is running). */
	const char *test_device = getenv("AHK_INPUTD_TEST_DEVICE");
	if (test_device && !*test_device)
		test_device = NULL;
	DIR *dir = opendir("/dev/input");
	if (!dir) { fprintf(stderr, "[inputd] /dev/input: %s\n", strerror(errno)); return; }
	struct dirent *ent;
	while ((ent = readdir(dir)) != NULL)
	{
		if (sPanicked || sReplayDead)
			break;
		if (strncmp(ent->d_name, "event", 5) != 0)
			continue;
		size_t device_name_len = strlen(ent->d_name);
		if (device_name_len >= sizeof(sGrabbedNames[0]))
			continue;
		int already = 0;
		for (int i = 0; i < sDevCount && !already; ++i)
			if (sGrabbedNames[i][0] && !strcmp(sGrabbedNames[i], ent->d_name))
				already = 1;
		if (already)
			continue;
		char path[PATH_MAX];
		if (snprintf(path, sizeof(path), "/dev/input/%s", ent->d_name) >= (int)sizeof(path))
			continue;
		int fd = open(path, O_RDONLY | O_NONBLOCK);
		if (fd < 0) continue;
		if (test_device)
		{
			/* comma-separated allow-list: node name, path, or "name:" device
			 * name substring (fixtures use predictable names, so hot-add
			 * oracles can pre-authorize devices that do not exist yet). */
			char list[512];
			snprintf(list, sizeof(list), "%s", test_device);
			int matched = 0;
			char *save = NULL;
			char *tok = strtok_r(list, ",", &save);
			while (tok && !matched)
			{
				while (*tok == ' ' || *tok == '\t') ++tok;
				if (!strncmp(tok, "name:", 5))
				{
					char devname[UINPUT_MAX_NAME_SIZE];
					memset(devname, 0, sizeof(devname));
					if (ioctl(fd, EVIOCGNAME(sizeof(devname) - 1), devname) >= 0
						&& strstr(devname, tok + 5) != NULL)
						matched = 1;
				}
				else if (!strcmp(tok, ent->d_name) || !strcmp(tok, path))
					matched = 1;
				tok = strtok_r(NULL, ",", &save);
			}
			if (!matched)
			{
				close(fd);
				continue; /* not a device under test */
			}
		}
		if (is_self_device(fd)) { close(fd); continue; }
		if (!is_keyboard(fd)) { close(fd); continue; }
		if (sDevCount < MAX_DEVICES)
		{
			/* two-phase grab (check0901 P0-1): EVIOCGKEY before AND after
			 * EVIOCGRAB.  Grabbing while a key is held makes the previous
			 * consumer miss the release (stuck key); a down appearing in the
			 * check->grab window is caught by the post-grab check, which
			 * ungrabs and defers the device to the next rescan. */
			int held_before = device_has_held_keys(fd);
			if (held_before != 0)
			{
				held_deferred = 1;
				fprintf(stderr, "[inputd] %s has held keys%s; grab deferred\n"
					, path, held_before < 0 ? " (state ambiguous)" : "");
				close(fd);
				continue;
			}
			/* Publish the fd before EVIOCGRAB so SIGALRM can always close a
			 * just-grabbed device even if it interrupts this ioctl. */
			int slot = sDevCount++;
			sDevFds[slot] = fd;
			memcpy(sGrabbedNames[slot], ent->d_name, device_name_len + 1);
			if (!sPanicked && !sReplayDead && ioctl(fd, EVIOCGRAB, 1) == 0 && !sPanicked)
			{
				if (device_has_held_keys(fd) != 0)
				{
					held_deferred = 1;
					/* Down appeared in the check->grab window: fail open
					 * immediately and retry on a later rescan. */
					ioctl(fd, EVIOCGRAB, 0);
					fprintf(stderr, "[inputd] %s: key down at grab boundary; "
						"grab deferred (fail-open)\n", path);
					close(fd);
					sDevFds[slot] = -1;
					sGrabbedNames[slot][0] = '\0';
					sDevIds[slot] = 0;
				}
				else
				{
					sAnyGrabbed = 1;
					sDevIds[slot] = stable_device_id(fd, path);
					logmsg("grabbed %s", path);
					broadcast_device_added(sDevIds[slot], ent->d_name);
				}
			}
			else
			{
				if (!sPanicked && !sReplayDead)
					fprintf(stderr, "[inputd] EVIOCGRAB %s: %s (running as root?)\n", path, strerror(errno));
				close(fd); // EBADF is harmless if SIGALRM already closed it.
				sDevFds[slot] = -1;
				sGrabbedNames[slot][0] = '\0';
				sDevIds[slot] = 0;
			}
		}
		else
			close(fd);
	}
	closedir(dir);
	prune_removed_devices();
	sHeldStateReconciled = held_deferred ? 0 : 1;
	if (sHeldStateReconciled != held_before_state)
		broadcast_backend_health();
}

static void prune_removed_devices(void)
{
	int write_index = 0;
	for (int read_index = 0; read_index < sDevCount; ++read_index)
	{
		if (sDevFds[read_index] < 0)
			continue;
		if (write_index != read_index)
		{
			sDevFds[write_index] = sDevFds[read_index];
			memcpy(sGrabbedNames[write_index], sGrabbedNames[read_index],
				sizeof(sGrabbedNames[write_index]));
			sDevIds[write_index] = sDevIds[read_index];
			memcpy(sPhysicalTxn[write_index], sPhysicalTxn[read_index],
				sizeof(sPhysicalTxn[write_index]));
			memcpy(sPhysicalOwnerAcceptance[write_index],
				sPhysicalOwnerAcceptance[read_index],
				sizeof(sPhysicalOwnerAcceptance[write_index]));
			memcpy(sPhysicalSuppressed[write_index],
				sPhysicalSuppressed[read_index],
				sizeof(sPhysicalSuppressed[write_index]));
		}
		++write_index;
	}
	for (int i = write_index; i < sDevCount; ++i)
	{
		sDevFds[i] = -1;
		sGrabbedNames[i][0] = '\0';
		sDevIds[i] = 0;
		clear_device_physical_state(i);
	}
	sDevCount = write_index;
	sAnyGrabbed = sDevCount > 0;
}

/* ---- systemd socket activation ------------------------------------------ */

static int adopt_activated_socket(char *path, size_t path_size)
{
	const char *pid_text = getenv("LISTEN_PID");
	const char *fds_text = getenv("LISTEN_FDS");
	if (!pid_text || !*pid_text || !fds_text || !*fds_text)
		return -1;
	char *end = NULL;
	long listen_pid = strtol(pid_text, &end, 10);
	if (!end || *end || listen_pid != (long)getpid())
		return -1;
	end = NULL;
	long listen_fds = strtol(fds_text, &end, 10);
	if (!end || *end || listen_fds != 1)
	{
		fprintf(stderr, "[inputd] socket activation requires exactly one fd (got %ld)\n", listen_fds);
		return -2;
	}
	int fd = 3; /* SD_LISTEN_FDS_START, without a libsystemd dependency. */
	int type = 0;
	socklen_t type_len = sizeof(type);
	int accepting = 0;
	socklen_t accepting_len = sizeof(accepting);
	struct sockaddr_un addr;
	socklen_t addr_len = sizeof(addr);
	memset(&addr, 0, sizeof(addr));
	if (getsockopt(fd, SOL_SOCKET, SO_TYPE, &type, &type_len) != 0
		|| type != SOCK_STREAM
		|| getsockopt(fd, SOL_SOCKET, SO_ACCEPTCONN, &accepting, &accepting_len) != 0
		|| !accepting
		|| getsockname(fd, (struct sockaddr *)&addr, &addr_len) != 0
		|| addr.sun_family != AF_UNIX)
	{
		fprintf(stderr, "[inputd] inherited fd 3 is not a listening UNIX stream socket\n");
		return -2;
	}
	if (!addr.sun_path[0])
	{
		fprintf(stderr, "[inputd] abstract activated sockets are not supported\n");
		return -2;
	}
	{
		size_t actual = strnlen(addr.sun_path, sizeof(addr.sun_path));
		if (actual >= sizeof(addr.sun_path) || actual + 1 > path_size)
		{
			fprintf(stderr, "[inputd] activated UNIX socket path is not terminated/bounded\n");
			return -2;
		}
		memcpy(path, addr.sun_path, actual + 1);
	}
	int flags = fcntl(fd, F_GETFD, 0);
	if (flags >= 0)
		fcntl(fd, F_SETFD, flags | FD_CLOEXEC);
	unsetenv("LISTEN_PID");
	unsetenv("LISTEN_FDS");
	unsetenv("LISTEN_FDNAMES");
	return fd;
}

/* ---- main ---------------------------------------------------------------- */

static void on_sig(int s)
{
	if (s == SIGALRM)
	{
		/* Async-signal-safe fail-open: close releases EVIOCGRAB in the kernel.
		 * The main loop observes EINTR, prunes these slots and logs afterward. */
		for (int i = 0; i < sDevCount; ++i)
			if (sDevFds[i] >= 0)
			{
				close(sDevFds[i]);
				sDevFds[i] = -1;
			}
		sAnyGrabbed = 0;
		sPanicked = 1; // permanent fail-open for this daemon instance.
		sWatchdogFired = 1;
	}
	else if (s == SIGTERM || s == SIGINT)
		sQuit = 1;
}

int main(int argc, char **argv)
{
	const char *socket_path = NULL;
	int protocol_only = 0;
	mode_t manual_socket_mode = 0600;
	char activated_path[sizeof(((struct sockaddr_un *)0)->sun_path)] = { 0 };
	int activated_fd = adopt_activated_socket(activated_path, sizeof(activated_path));
	if (activated_fd == -2)
		return 1;
	int socket_activated = activated_fd >= 0;
	for (int i = 1; i < argc; ++i)
	{
		if (!strcmp(argv[i], "--socket") && i + 1 < argc)
			socket_path = argv[++i];
		else if (!strcmp(argv[i], "-v"))
			sVerbose = 1;
		else if (!strcmp(argv[i], "--protocol-only"))
			protocol_only = 1;
		else if (!strcmp(argv[i], "--socket-mode") && i + 1 < argc)
		{
			char *end = NULL;
			long mode = strtol(argv[++i], &end, 8);
			if (!end || *end || mode < 0 || mode > 0777)
			{
				fprintf(stderr, "[inputd] invalid --socket-mode (expected octal 0000..0777)\n");
				return 2;
			}
			manual_socket_mode = (mode_t)mode;
		}
		else
		{
			fprintf(stderr, "usage: %s [--socket PATH] [--socket-mode OCTAL] [--protocol-only] [-v]\n", argv[0]);
			return 2;
		}
	}
	if (socket_activated && activated_path[0])
	{
		if (socket_path && strcmp(socket_path, activated_path) != 0)
		{
			fprintf(stderr, "[inputd] --socket %s does not match activated %s\n",
				socket_path, activated_path);
			return 1;
		}
		socket_path = activated_path;
	}
	if (!socket_path)
	{
		const char *rt = getenv("XDG_RUNTIME_DIR");
		static char def[sizeof(((struct sockaddr_un *)0)->sun_path)];
		if (snprintf(def, sizeof(def), "%s/ahk-inputd.sock", (rt && *rt) ? rt : "/tmp") >= (int)sizeof(def))
		{
			fprintf(stderr, "[inputd] default socket path exceeds UNIX sun_path\n");
			return 1;
		}
		socket_path = def;
	}
	if (strlen(socket_path) >= sizeof(((struct sockaddr_un *)0)->sun_path))
	{
		fprintf(stderr, "[inputd] socket path too long: %s\n", socket_path);
		return 1;
	}

	/* Manual mode needs its own singleton lock. Socket activation already
	 * serializes one service against one inherited descriptor and does not need
	 * write access anywhere under /run. */
	char lock_path[320] = { 0 };
	int lock_fd = -1;
	if (!socket_activated)
	{
		snprintf(lock_path, sizeof(lock_path), "%s.lock", socket_path);
		lock_fd = open(lock_path, O_CREAT | O_RDWR, 0600);
		if (lock_fd < 0 || flock(lock_fd, LOCK_EX | LOCK_NB) != 0)
		{
			fprintf(stderr, "[inputd] another instance holds %s\n", lock_path);
			return 1;
		}
	}
	if (socket_activated)
	{
		sListenFd = activated_fd;
		fprintf(stderr, "[inputd] adopted systemd socket %s\n", socket_path);
	}
	else
	{
		unlink(socket_path);
		sListenFd = socket(AF_UNIX, SOCK_STREAM, 0);
		struct sockaddr_un addr;
		memset(&addr, 0, sizeof(addr));
		addr.sun_family = AF_UNIX;
		memcpy(addr.sun_path, socket_path, strlen(socket_path) + 1);
		if (sListenFd < 0 || bind(sListenFd, (struct sockaddr *)&addr, sizeof(addr)) != 0
			|| listen(sListenFd, 8) != 0)
		{
			fprintf(stderr, "[inputd] bind/listen %s: %s\n", socket_path, strerror(errno));
			return 1;
		}
		if (chmod(socket_path, manual_socket_mode) != 0)
		{
			fprintf(stderr, "[inputd] chmod %s: %s\n", socket_path, strerror(errno));
			close(sListenFd);
			unlink(socket_path);
			return 1;
		}
	}
	/* check0901 P0-3: broker event identity.  Fresh authority+generation per
	 * process so a restarted broker can never be confused with its previous
	 * incarnation (old event_seq / transaction claims are inherently stale). */
	{
		int ur = open("/dev/urandom", O_RDONLY);
		unsigned char identity[24];
		ssize_t rd = 0;
		if (ur >= 0)
		{
			rd = read(ur, identity, sizeof(identity));
			close(ur);
		}
		if (rd != (ssize_t)sizeof(identity))
		{
			/* urandom unavailable: degrade to a per-boot-ish mixture. */
			unsigned long long mix = (unsigned long long)now_us()
				^ ((unsigned long long)getpid() << 32) ^ 0x9e3779b97f4a7c15ULL;
			for (int i = 0; i < 24; ++i)
				identity[i] = (unsigned char)(mix >> ((i % 8) * 8));
		}
		memcpy(sAuthorityId, identity, 16);
		memcpy(&sGeneration, identity + 16, 8);
		if (!sGeneration)
			sGeneration = 1;
	}
	for (int i = 0; i < MAX_CLIENTS; ++i)
	{
		sClients[i].fd = -1;
		sClients[i].pid = 0;
		sClients[i].uid = 0;
		sClients[i].gid = 0;
		sClients[i].rx_used = 0;
		sClients[i].hello_ok = 0;
		sClients[i].rules[0].code = 0;
		sClients[i].v2 = 0;
		sClients[i].v2_hello_ok = 0;
		sClients[i].rules_confirmed = 0;
		sClients[i].v2_client_seq_last = 0;
		sClients[i].client_id = 0;
		sClients[i].caps_granted = 0;
		memset(sClients[i].nonce, 0, sizeof(sClients[i].nonce));
	}
	for (int i = 0; i < MAX_DEVICES; ++i)
	{
		sDevFds[i] = -1;
		sDevIds[i] = 0;
	}
	memset(sGrabbedNames, 0, sizeof(sGrabbedNames));

	signal(SIGTERM, on_sig);
	signal(SIGINT, on_sig);
	signal(SIGALRM, on_sig); /* watchdog: release grabs, keep serving */
	signal(SIGPIPE, SIG_IGN);

	/* Test-only fault injection knobs (check0901 P0-1 oracles). */
	const char *fail_after_text = getenv("AHK_INPUTD_TEST_REPLAY_FAIL_AFTER");
	if (fail_after_text && *fail_after_text)
	{
		char *end = NULL;
		sReplayFailAfter = strtol(fail_after_text, &end, 10);
		if (!end || *end || sReplayFailAfter < 1)
		{
			fprintf(stderr, "[inputd] invalid AHK_INPUTD_TEST_REPLAY_FAIL_AFTER\n");
			return 2;
		}
		fprintf(stderr, "[inputd] test hook: replay will fail after %ld batch(es)\n"
			, sReplayFailAfter);
	}

	sProtocolOnly = protocol_only;
	sHeldStateReconciled = protocol_only ? 1 : 0;
	if (protocol_only)
		sLastSuccessUs = (unsigned long long)now_us();
	if (!protocol_only)
	{
		/* check0901 P0-1: create AND validate the replay device BEFORE any
		 * physical grab.  Without a working replay lane we run listen-only
		 * (no grabs) so normal input is never swallowed. */
		sUinputFd = open_uinput_replay();
		if (sUinputFd < 0)
		{
			sReplayDead = 1;
			fprintf(stderr, "[inputd] replay unavailable: listen-only degraded mode (no grabs)\n");
			fprintf(stderr, "STATUS=degraded: replay unavailable, grabs disabled\n");
		}
		else
		{
			sLastSuccessUs = (unsigned long long)now_us();
			sReplayErrno = 0;
			scan_devices();
		}
	}
	if (!protocol_only)
	{
		/* check0905: Type=notify readiness is emitted only after replay
		 * preflight and the initial device scan have completed. */
		systemd_notify(sReplayDead
			? "READY=1\nSTATUS=degraded: replay unavailable, grabs disabled"
			: "READY=1\nSTATUS=healthy: replay ready and device scan complete");
	}
	fprintf(stderr, "[inputd] ready on %s (%d keyboard(s) grabbed%s%s)\n",
		socket_path, sDevCount,
		protocol_only ? ", protocol-only" : "",
		(!protocol_only && sReplayDead) ? ", degraded listen-only" : "");

	struct pollfd pfds[1 + MAX_DEVICES + MAX_CLIENTS];
	int polled_devices[MAX_DEVICES];
	int polled_clients[MAX_CLIENTS];
	struct timespec timeout = { 0, 200 * 1000 * 1000 };
	long long next_rescan = now_ms() + 1000;
	long long activated_idle_since = 0;
	while (!sQuit)
	{
		int n = 0;
		int polled_device_count = 0;
		int polled_client_count = 0;
		pfds[n].fd = sListenFd; pfds[n].events = POLLIN; pfds[n].revents = 0; ++n;
		for (int i = 0; i < sDevCount; ++i)
			if (sDevFds[i] >= 0)
			{
				polled_devices[polled_device_count++] = i;
				pfds[n].fd = sDevFds[i]; pfds[n].events = POLLIN; pfds[n].revents = 0; ++n;
			}
		for (int i = 0; i < MAX_CLIENTS; ++i)
			if (sClients[i].fd >= 0)
			{
				polled_clients[polled_client_count++] = i;
				pfds[n].fd = sClients[i].fd; pfds[n].events = POLLIN; pfds[n].revents = 0; ++n;
			}
		int pr = ppoll(pfds, (nfds_t)n, &timeout, NULL);
		/* watchdog heartbeat: returning to this point proves liveness */
		alarm(2);
		if (sWatchdogFired)
		{
			sWatchdogFired = 0;
			fprintf(stderr, "[inputd] watchdog: loop stalled; grabs closed fail-open\n");
			prune_removed_devices();
		}
		long long now = now_ms();
		if (pr < 0)
		{
			if (errno == EINTR) continue;
			break;
		}
		/* M4: lazy transaction TTL sweep (250 ms cadence). */
		if (now - sLastTtlSweepMs >= 250)
		{
			sLastTtlSweepMs = now;
			sweep_inject_ttl();
			sweep_arb_leases();
		}
		int idx = 0;
		if (pfds[idx].revents & POLLIN)
			(void)accept_client();
		++idx; /* listen slot is always present, with or without events */
		for (int slot = 0; slot < polled_device_count; ++slot)
		{
			int i = polled_devices[slot];
			short device_events = pfds[idx++].revents;
			if (sDevFds[i] < 0)
				continue;
			bool remove_device = (device_events & (POLLERR | POLLHUP | POLLNVAL)) != 0;
			if (device_events & POLLIN)
			{
				struct input_event ev;
				for (;;)
				{
					ssize_t rd = read(sDevFds[i], &ev, sizeof(ev));
					if (rd != (ssize_t)sizeof(ev))
					{
						if (rd == 0 || (rd < 0 && errno != EAGAIN
							&& errno != EWOULDBLOCK && errno != EINTR))
							remove_device = true;
						break;
					}
					if (ev.type == EV_SYN && ev.code == SYN_DROPPED)
					{
						int held = device_has_held_keys(sDevFds[i]);
						clear_device_physical_state(i);
						sHeldStateReconciled = 0;
						fprintf(stderr,
							"[inputd] %s: SYN_DROPPED; dropping queued frame "
							"and reopening device (held=%d)\n",
							sGrabbedNames[i], held);
						// No reliable key-up ownership survives a dropped frame.
						// Close/ungrab now: future input is fail-open, and the next
						// scan uses EVIOCGKEY before attempting another grab.
						remove_device = true;
						break;
					}
					if (ev.type != EV_KEY) continue;
					if (panic_step((unsigned int)ev.code, ev.value != 0))
						continue;
					if (sPanicked) continue; /* fail-open: pass through */
					/* Stamp the broker-lane identity once per event (check0901
					 * P0-3): authority+generation+seq are shared by every
					 * recipient, so A/B scripts observe identical provenance. */
					++sEventSeq;
					unsigned long long source_event_seq = sEventSeq;
					long long ev_ts = now_us();
					unsigned long long ev_dev_id = sDevIds[i];
					unsigned int code = (unsigned int)ev.code;
					int down = ev.value != 0;
					if (code < KEY_CNT)
					{
						if (down && ev.value != 2 && !sPhysicalTxn[i][code])
							sPhysicalTxn[i][code] = ++sNextBrokerTxnId;
					}
					unsigned long long source_txn = code < KEY_CNT ? sPhysicalTxn[i][code] : 0;
					/* distribute to subscribed clients first */
					int legacy_suppress = 0;
					for (int c = 0; c < MAX_CLIENTS; ++c)
					{
						struct client *cl = &sClients[c];
						if (cl->fd < 0 || cl->dead) continue;
						for (int r = 0; r < cl->rule_count; ++r)
							if (cl->rules[r].code == code)
							{
								if (cl->rules[r].suppress)
									legacy_suppress = 1;
								if (cl->v2 && cl->v2_hello_ok)
									send_event_v2(cl, code, ev.value, ev_ts, ev_dev_id
										, 0, source_txn, 0, -1, INPUTD_V2_SOURCE_PHYSICAL);
								else
									send_event_to(cl, code, ev.value, ev_ts);
								break;
							}
					}
					struct arb_rule *winner = arb_winner(code);
					if (code < KEY_CNT && sPhysicalSuppressed[i][code]
						&& (ev.value == 2 || !down))
						winner = arb_by_acceptance(sPhysicalOwnerAcceptance[i][code]);
					int suppress = legacy_suppress;
					unsigned char action = legacy_suppress
						? INPUTD_V2_DECISION_SUPPRESS : INPUTD_V2_DECISION_REPLAY;
					unsigned char reason = legacy_suppress
						? INPUTD_V2_DECISION_LEGACY_SUPPRESS : INPUTD_V2_DECISION_NONE;
					unsigned long long winner_id = 0;
					unsigned long long replacement_txn = 0;
					int winner_priority = 0;
					int dynamic_resolved = 0;
					if (winner && winner->dynamic_decision)
					{
						if (ev.value == 1)
						{
							unsigned long long deadline_us = (unsigned long long)now_us()
								+ INPUTD_V2_DYNAMIC_DEADLINE_MS * 1000ULL;
							unsigned long long excluded[INPUTD_V2_ARB_MAX_RULES];
							int excluded_count = 0;
							unsigned char last_reason = INPUTD_V2_DECISION_DYNAMIC_FALSE;
							unsigned long long last_id = 0;
							int last_priority = 0;
							while (winner && winner->dynamic_decision)
							{
								last_id = winner->registration_id;
								last_priority = winner->priority;
								int dynamic_suppress = 0;
								if ((unsigned long long)now_us() < deadline_us)
									dynamic_suppress = wait_dynamic_decision(winner,
										source_event_seq, source_txn, code,
										(unsigned int)ev.value, deadline_us, &last_reason);
								else
									last_reason = INPUTD_V2_DECISION_DYNAMIC_TIMEOUT;
								if (dynamic_suppress)
								{
									suppress = 1;
									action = INPUTD_V2_DECISION_SUPPRESS;
									reason = last_reason;
									winner_id = last_id;
									winner_priority = last_priority;
									dynamic_resolved = 1;
									break;
								}
								excluded[excluded_count++] = winner->acceptance_seq;
								winner = arb_winner_filtered(code, excluded,
									excluded_count);
							}
							if (!dynamic_resolved && !winner)
							{
								suppress = legacy_suppress;
								action = suppress ? INPUTD_V2_DECISION_SUPPRESS
									: INPUTD_V2_DECISION_REPLAY;
								reason = suppress ? INPUTD_V2_DECISION_LEGACY_SUPPRESS
									: last_reason;
								winner_id = last_id;
								winner_priority = last_priority;
								dynamic_resolved = 1;
							}
						}
						else if (code < KEY_CNT && sPhysicalSuppressed[i][code])
						{
							suppress = 1;
							action = INPUTD_V2_DECISION_SUPPRESS;
							reason = INPUTD_V2_DECISION_STICKY_KEYUP;
							winner_id = winner->registration_id;
							winner_priority = winner->priority;
							dynamic_resolved = 1;
						}
						else
						{
							suppress = legacy_suppress;
							action = suppress ? INPUTD_V2_DECISION_SUPPRESS
								: INPUTD_V2_DECISION_REPLAY;
							reason = suppress ? INPUTD_V2_DECISION_LEGACY_SUPPRESS
								: INPUTD_V2_DECISION_DYNAMIC_FALSE;
							winner_id = winner->registration_id;
							winner_priority = winner->priority;
							dynamic_resolved = 1;
						}
					}
					/* A suppressed down owns its matching up even if the registration
					 * disappears before release (balanced target sequence). */
					if (!dynamic_resolved && code < KEY_CNT && sPhysicalSuppressed[i][code]
						&& (ev.value == 2 || !down) && !winner)
					{
						suppress = 1;
						action = INPUTD_V2_DECISION_SUPPRESS;
						reason = INPUTD_V2_DECISION_STICKY_KEYUP;
					}
					else if (winner && !dynamic_resolved)
					{
						suppress = 1;
						winner_id = winner->registration_id;
						winner_priority = winner->priority;
						if (winner->mode == INPUTD_V2_ARB_REMAP)
						{
							action = INPUTD_V2_DECISION_REMAP;
							reason = INPUTD_V2_DECISION_STATIC_REMAP;
							if (down && ev.value != 2)
							{
								replacement_txn = ++sNextBrokerTxnId;
								winner->active_replacement_txn = replacement_txn;
								winner->active_parent_txn = source_txn;
								winner->replacement_down = 1;
							}
							else
								replacement_txn = winner->active_replacement_txn;
							if (replacement_txn)
							{
								struct inject_txn synthetic;
								memset(&synthetic, 0, sizeof(synthetic));
								synthetic.owner = winner->owner;
								synthetic.id = replacement_txn;
								synthetic.parent = winner->active_parent_txn
									? winner->active_parent_txn : source_txn;
								synthetic.send_level = winner->replacement_send_level;
								if (replay_key(winner->replacement_code, ev.value) != 0)
								{
									action = INPUTD_V2_DECISION_REPLACEMENT_FAILED;
									reason = INPUTD_V2_DECISION_STATIC_REMAP;
									remove_device = true;
								}
								else
									inject_publish(winner->owner, &synthetic,
										winner->replacement_code, ev.value);
							}
							if (!down)
							{
								winner->replacement_down = 0;
								winner->active_replacement_txn = 0;
								winner->active_parent_txn = 0;
							}
						}
						else
						{
							action = INPUTD_V2_DECISION_SUPPRESS;
							reason = winner->mode == INPUTD_V2_ARB_EXCLUSIVE
								? INPUTD_V2_DECISION_EXCLUSIVE
								: INPUTD_V2_DECISION_STATIC_SUPPRESS;
						}
					}
					if (code < KEY_CNT)
					{
						if (down && ev.value != 2)
						{
							sPhysicalSuppressed[i][code] = suppress ? 1 : 0;
							sPhysicalOwnerAcceptance[i][code] = winner
								? winner->acceptance_seq : 0;
						}
						else if (!down)
						{
							sPhysicalSuppressed[i][code] = 0;
							sPhysicalOwnerAcceptance[i][code] = 0;
							sPhysicalTxn[i][code] = 0;
						}
					}
					if (reason == INPUTD_V2_DECISION_DYNAMIC_DISCONNECT)
						logmsg("dynamic decision event %llu owner disconnected; fail-open",
							source_event_seq);
					broadcast_decision(source_event_seq, source_txn, code, action, reason,
						winner_id, replacement_txn, winner_priority);
					if (!suppress)
					{
						if (replay_key(code, ev.value) != 0)
						{
							/* replay_dead() already released every grab and
							 * closed the device fds; drop this slot too. */
							remove_device = true;
						}
					}
				}
			}
			if (remove_device)
			{
				clear_device_physical_state(i);
				ioctl(sDevFds[i], EVIOCGRAB, 0);
				fprintf(stderr, "[inputd] hot-remove %s\n", sGrabbedNames[i]);
				if (sDevIds[i])
					broadcast_device_removed(sDevIds[i]);
				close(sDevFds[i]);
				sDevFds[i] = -1;
				sGrabbedNames[i][0] = '\0';
				sDevIds[i] = 0;
			}
		}
		prune_removed_devices();
		for (int slot = 0; slot < polled_client_count; ++slot)
		{
			int i = polled_clients[slot];
			struct client *cl = &sClients[i];
			short client_events = pfds[idx++].revents;
			if (cl->fd < 0)
				continue;
			if (client_events & (POLLIN | POLLHUP | POLLERR))
				drain_client(cl);
		}
		/* drop clients that died mid-write */
		for (int i = 0; i < MAX_CLIENTS; ++i)
			if (sClients[i].fd >= 0 && sClients[i].dead)
				close_client(&sClients[i]);
		/* Mutate the device array only after consuming the ppoll snapshot. */
		now = now_ms();
		if (!protocol_only && !sPanicked && now >= next_rescan)
		{
			next_rescan = now + 1000;
			scan_devices();
		}
		if (socket_activated)
		{
			if (active_client_count() > 0)
				activated_idle_since = 0;
			else if (!activated_idle_since)
				activated_idle_since = now_ms();
			else if (now_ms() - activated_idle_since >= ACTIVATED_IDLE_EXIT_MS)
			{
				fprintf(stderr, "[inputd] no clients for %dms; exiting to socket activation\n",
					ACTIVATED_IDLE_EXIT_MS);
				break;
			}
		}
	}
	release_all_grabs();
	close(sListenFd);
	if (!socket_activated)
		unlink(socket_path);
	if (sUinputFd >= 0) { ioctl(sUinputFd, UI_DEV_DESTROY); close(sUinputFd); }
	for (int i = 0; i < MAX_CLIENTS; ++i)
		if (sClients[i].fd >= 0) close_client(&sClients[i]);
	if (lock_fd >= 0)
	{
		close(lock_fd);
		unlink(lock_path); // Only the successful manual lock owner removes it.
	}
	fprintf(stderr, "[inputd] exited\n");
	return 0;
}
