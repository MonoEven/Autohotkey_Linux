/* inputd_v2_probe.c -- protocol v2 test client for ahk-inputd.
 *
 * INDEPENDENT implementation of the v2 wire format (check0901 P0-3): it
 * serializes every field byte by byte in little-endian, never trusting a
 * broker and never sharing code with the daemon.  Used by the protocol
 * oracle to prove negotiation, framing, provenance and capability denial.
 *
 * Usage: inputd_v2_probe SOCKET MODE [args]
 *   hello [--caps N] [--nonce HEX] [--proto N] [--seq N]
 *         [--expect-error] [--expect-close]
 *   hello2                 HELLO twice -> expect ERROR DUPLICATE_HELLO
 *   nothello               SUBSCRIBE first -> expect ERROR NOT_HELLOED
 *   seqviol                non-monotonic client_seq -> ERROR SEQUENCE_VIOLATION
 *   badmagic               garbage magic -> connection closed
 *   oversized              message_len beyond the max frame -> closed
 *   badtype                unknown message type -> ERROR BAD_FRAME
 *   sub RULES              HELLO + SUBSCRIBE (expect SUBSCRIBE_ACK)
 *   deny-sup               HELLO(caps=OBSERVE|SUPPRESS) + SUBSCRIBE "30:1"
 *                          -> expect ERROR CAPABILITY_DENIED
 *   inject SOCKET CODE LEVEL [--txn N] [--down-only] [--begin-only]
 *          [--pairs N] [--no-commit] [--stay MS]
 *   inject-bad-phase SOCKET CODE LEVEL -> BAD_FRAME for phase/value mismatch
 *   inject-incomplete SOCKET CODE LEVEL -> BAD_FRAME for short COMMIT plan
 *   watch RULES [--until-events N] [--timeout-ms N]
 *
 * Prints machine-greppable lines:
 *   SUBSCRIBE_ACK ok=N granted=N
 *   ERROR code=N detail=...
 *   EVENT seq=N ts=N dev=N src=N conf=N level=D code=N phase=N value=N
 *   DEVICE_ADDED id=N name=...
 *   DEVICE_REMOVED id=N
 *   BACKEND_DEGRADED
 *   CLOSED / NO_RESPONSE
 */
#define _POSIX_C_SOURCE 200809L
#define _DEFAULT_SOURCE /* usleep */
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/time.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <poll.h>

#define V2_MAGIC 0x324B4841u
#define V2_VERSION 2u
#define V2_HEADER_LEN 28u
#define V2_MAX_PAYLOAD 16384u
#define V2_MAX_FRAME (4 + V2_HEADER_LEN + V2_MAX_PAYLOAD)

#define MSG_HELLO 1u
#define MSG_HELLO_ACK 2u
#define MSG_SUBSCRIBE 3u
#define MSG_SUBSCRIBE_ACK 4u
#define MSG_UNSUBSCRIBE 5u
#define MSG_UNSUBSCRIBE_ACK 6u
#define MSG_EVENT 7u
#define MSG_PING 8u
#define MSG_PONG 9u
#define MSG_ERROR 10u
#define MSG_DEVICE_ADDED 11u
#define MSG_DEVICE_REMOVED 12u
#define MSG_BACKEND_DEGRADED 13u
#define MSG_INJECT_BEGIN 14u
#define MSG_INJECT_EVENT 15u
#define MSG_INJECT_COMMIT 16u
#define MSG_INJECT_ABORT 17u
#define MSG_INJECT_ACK 18u
#define MSG_ARB_REGISTER 19u
#define MSG_ARB_REGISTER_ACK 20u
#define MSG_ARB_UNREGISTER 21u
#define MSG_CONFLICT 22u
#define MSG_ARB_DECISION 23u
#define MSG_BACKEND_HEALTH 24u
#define MSG_DECISION_REQUEST 25u
#define MSG_DECISION_REPLY 26u

#define CAP_OBSERVE 0x1u
#define CAP_SUPPRESS 0x2u
#define CAP_EXCLUSIVE 0x4u
#define CAP_INJECT 0x8u

#define ARB_OBSERVE 0u
#define ARB_SUPPRESS 1u
#define ARB_EXCLUSIVE 2u
#define ARB_REMAP 3u
#define ARB_POLICY_REJECT 0u
#define ARB_POLICY_PREEMPT 1u
#define ARB_GRANTED 0u
#define ARB_REFRESHED 1u
#define ARB_UNREGISTERED 2u
#define ARB_CONFLICTED 3u
#define ARB_DENIED 4u
#define ARB_BAD_FRAME 5u
#define ARB_QUOTA 6u
#define ARB_EXPIRED 7u

#define INJECT_OK_BEGIN 0u
#define INJECT_OK_COMMIT 1u
#define INJECT_OK_ABORT 2u
#define INJECT_STALE 3u
#define INJECT_DENIED 4u
#define INJECT_QUOTA 5u
#define INJECT_BAD_FRAME 6u
#define INJECT_DEGRADED 7u
#define INJECT_OK_EVENT 8u

#define ERR_PROTO_UNSUPPORTED 1u
#define ERR_BAD_FRAME 2u
#define ERR_NOT_HELLOED 3u
#define ERR_CAPABILITY_DENIED 4u
#define ERR_SEQUENCE_VIOLATION 5u
#define ERR_DUPLICATE_HELLO 6u

static void st_le16(unsigned char *p, unsigned int v)
{ p[0] = (unsigned char)v; p[1] = (unsigned char)(v >> 8); }

static void st_le32(unsigned char *p, unsigned int v)
{ p[0]=(unsigned char)v; p[1]=(unsigned char)(v>>8); p[2]=(unsigned char)(v>>16); p[3]=(unsigned char)(v>>24); }

static void st_le64(unsigned char *p, unsigned long long v)
{ st_le32(p, (unsigned int)v); st_le32(p + 4, (unsigned int)(v >> 32)); }

static unsigned int ld_le16(const unsigned char *p)
{ return (unsigned int)p[0] | ((unsigned int)p[1] << 8); }

static unsigned int ld_le32(const unsigned char *p)
{ return (unsigned int)p[0] | ((unsigned int)p[1] << 8) | ((unsigned int)p[2] << 16) | ((unsigned int)p[3] << 24); }

static unsigned long long ld_le64(const unsigned char *p)
{ return (unsigned long long)ld_le32(p) | ((unsigned long long)ld_le32(p + 4) << 32); }

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

static int read_full(int fd, void *buf, size_t n)
{
	size_t got = 0;
	while (got < n)
	{
		ssize_t r = read(fd, (char *)buf + got, n - got);
		if (r == 0) return 0;
		if (r < 0) { if (errno == EINTR) continue; return -1; }
		got += (size_t)r;
	}
	return 1;
}

static int send_frame(int fd, unsigned int mtype, unsigned long long client_seq
	, const unsigned char *payload, size_t payload_len)
{
	unsigned char frame[V2_MAX_FRAME];
	unsigned char *p = frame;
	st_le32(p, V2_MAGIC); p += 4;
	st_le16(p, V2_VERSION); p += 2;
	st_le16(p, V2_HEADER_LEN); p += 2;
	st_le32(p, V2_HEADER_LEN + (unsigned int)payload_len); p += 4;
	st_le16(p, mtype); p += 2;
	st_le16(p, 0); p += 2;
	st_le64(p, 0); p += 8;
	st_le64(p, client_seq); p += 8;
	if (payload_len) memcpy(p, payload, payload_len);
	return write_full(fd, frame, 4 + V2_HEADER_LEN + payload_len);
}

static int send_hello(int fd, unsigned int proto, unsigned int caps
	, unsigned long long seq, const unsigned char *nonce, int have_nonce)
{
	unsigned char payload[30];
	memset(payload, 0, sizeof(payload));
	st_le16(payload, proto);
	st_le16(payload + 2, proto);
	if (have_nonce)
		memcpy(payload + 4, nonce, 16);
	else
	{
		int b;
		for (b = 0; b < 16; ++b) payload[4 + b] = (unsigned char)(0x40 + b);
	}
	st_le32(payload + 20, caps);
	st_le16(payload + 24, 1);
	st_le16(payload + 26, 1);
	st_le16(payload + 28, 1024);
	return send_frame(fd, MSG_HELLO, seq, payload, sizeof(payload));
}

/* Read one frame; returns 0 when closed, -1 on timeout/protocol error. */
static int read_frame(int fd, unsigned int *mtype, unsigned char *payload
	, size_t *payload_len)
{
	unsigned char head[4 + V2_HEADER_LEN];
	int r = read_full(fd, head, sizeof(head));
	if (r != 1)
		return r;
	if (ld_le32(head) != V2_MAGIC || ld_le16(head + 4) != V2_VERSION
		|| ld_le16(head + 6) != V2_HEADER_LEN)
		return -1;
	unsigned int mlen = ld_le32(head + 8);
	if (mlen < V2_HEADER_LEN || mlen - V2_HEADER_LEN > V2_MAX_PAYLOAD)
		return -1;
	size_t plen = (size_t)mlen - V2_HEADER_LEN;
	if (plen && read_full(fd, payload, plen) != 1)
		return -1;
	*mtype = ld_le16(head + 12);
	*payload_len = plen;
	return 1;
}

static void print_ack(const unsigned char *p, size_t n)
{
	if (n < 52u) { printf("HELLO_ACK_SHORT\n"); return; }
	printf("HELLO_ACK proto=%u client_id=%llu authority=%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x "
		"generation=%llu caps=0x%x denied=0x%x flags=0x%x seq_start=%llu\n",
		ld_le16(p), ld_le64(p + 2),
		p[10], p[11], p[12], p[13], p[14], p[15], p[16], p[17],
		p[18], p[19], p[20], p[21], p[22], p[23], p[24], p[25],
		ld_le64(p + 26), ld_le32(p + 34), ld_le32(p + 38),
		ld_le16(p + 50), ld_le64(p + 42));
	fflush(stdout);
}

static void print_error(const unsigned char *p, size_t n)
{
	if (n < 6u) { printf("ERROR_SHORT\n"); return; }
	unsigned int code = ld_le32(p);
	size_t dlen = ld_le16(p + 4);
	if (dlen > n - 6) dlen = n - 6;
	printf("ERROR code=%u detail=%.*s\n", code, (int)dlen, (const char *)p + 6);
	fflush(stdout);
}

static void print_event(const unsigned char *p)
{
	/* envelope 82 + key payload 16 */
	const unsigned char *key = p + 82;
	printf("EVENT seq=%llu ts=%llu dev=%llu src=%u conf=%u level=%d txn=%llu prod=%llu parent=%llu code=%u phase=%u value=%u\n",
		ld_le64(p + 24), ld_le64(p + 32), ld_le64(p + 40),
		(unsigned)p[48], (unsigned)p[50], (int)(short)ld_le16(p + 52),
		ld_le64(p + 66), ld_le64(p + 58), ld_le64(p + 74),
		ld_le32(key), (unsigned)key[12], (unsigned)key[13]);
	fflush(stdout);
}

static void print_inject_ack(const unsigned char *p, size_t n)
{
	if (n < 11u) { printf("INJECT_ACK_SHORT\n"); return; }
	unsigned int dlen = ld_le16(p + 9);
	if (dlen > n - 11) dlen = (unsigned int)(n - 11);
	printf("INJECT_ACK txn=%llu status=%u detail=%.*s\n",
		ld_le64(p), (unsigned)p[8], (int)dlen, (const char *)p + 11);
	fflush(stdout);
}

static void print_arb_ack(const unsigned char *p, size_t n)
{
	if (n < 33u) { printf("ARB_ACK_SHORT\n"); return; }
	printf("ARB_ACK reg=%llu status=%u owner=%llu acceptance=%llu expiry=%llu\n",
		ld_le64(p), (unsigned)p[8], ld_le64(p + 9), ld_le64(p + 17),
		ld_le64(p + 25));
	fflush(stdout);
}

static void print_conflict(const unsigned char *p, size_t n)
{
	if (n < 33u) { printf("CONFLICT_SHORT\n"); return; }
	printf("CONFLICT requested=%llu owner=%llu owner_client=%llu code=%u reason=%u owner_priority=%d requester_priority=%d\n",
		ld_le64(p), ld_le64(p + 8), ld_le64(p + 16), ld_le32(p + 24),
		(unsigned)p[28], (int)(short)ld_le16(p + 29),
		(int)(short)ld_le16(p + 31));
	fflush(stdout);
}

static void print_decision(const unsigned char *p, size_t n)
{
	if (n < 40u) { printf("DECISION_SHORT\n"); return; }
	printf("DECISION seq=%llu source_txn=%llu code=%u action=%u reason=%u winner=%llu replacement_txn=%llu priority=%d\n",
		ld_le64(p), ld_le64(p + 8), ld_le32(p + 16),
		(unsigned)p[20], (unsigned)p[21], ld_le64(p + 22),
		ld_le64(p + 30), (int)(short)ld_le16(p + 38));
	fflush(stdout);
}

static void print_health(const unsigned char *p, size_t n)
{
	if (n < 50u) { printf("HEALTH_SHORT\n"); return; }
	size_t rlen = ld_le16(p + 48);
	if (rlen > n - 50) rlen = n - 50;
	printf("HEALTH state=%u permission=%u flags=0x%x authority_generation=%llu health_seq=%llu last_success=%llu errno=%d devices=%u grabbed=%u registrations=%u transactions=%u reason=%.*s\n",
		(unsigned)p[0], (unsigned)p[1], ld_le16(p + 2),
		ld_le64(p + 4), ld_le64(p + 12), ld_le64(p + 20),
		(int)ld_le32(p + 28), ld_le32(p + 32), ld_le32(p + 36),
		ld_le32(p + 40), ld_le32(p + 44), (int)rlen,
		(const char *)p + 50);
	fflush(stdout);
}

static void key_payload_fill(unsigned int code, int value, unsigned char *out)
{
	memset(out, 0, 16);
	st_le32(out, code);
	out[12] = (unsigned char)(value == 2 ? 2 : (value ? 0 : 1));
	out[13] = (unsigned char)value;
}

static int parse_rules(const char *rules, unsigned int *codes, unsigned char *sup, int cap)
{
	int count = 0;
	char *copy = strdup(rules);
	char *tok = strtok(copy, ",");
	while (tok && count < cap)
	{
		char *colon = strchr(tok, ':');
		codes[count] = (unsigned int)atoi(tok);
		sup[count] = colon && colon[1] == '1' ? 1 : 0;
		++count;
		tok = strtok(NULL, ",");
	}
	free(copy);
	return count;
}

static int read_one_print(int fd, unsigned int *mtype, unsigned char *rp
	, size_t *rp_len)
{
	int r = read_frame(fd, mtype, rp, rp_len);
	if (r == 0) { printf("CLOSED\n"); fflush(stdout); return 0; }
	if (r < 0) { printf("NO_RESPONSE\n"); fflush(stdout); return -1; }
	return 1;
}

int main(int argc, char **argv)
{
	if (argc < 3)
	{
		fprintf(stderr, "usage: %s SOCKET MODE [args]\n", argv[0]);
		return 2;
	}
	const char *socket_path = argv[1];
	const char *mode = argv[2];

	int fd = socket(AF_UNIX, SOCK_STREAM, 0);
	struct sockaddr_un addr;
	memset(&addr, 0, sizeof(addr));
	addr.sun_family = AF_UNIX;
	strncpy(addr.sun_path, socket_path, sizeof(addr.sun_path) - 1);
	if (fd < 0 || connect(fd, (struct sockaddr *)&addr, sizeof(addr)) != 0)
	{
		fprintf(stderr, "v2_probe: connect %s: %s\n", socket_path, strerror(errno));
		return 1;
	}
	struct timeval tv = { 3, 0 };
	setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

	unsigned char payload[V2_MAX_FRAME];
	unsigned char rp[V2_MAX_FRAME];
	size_t rp_len = 0;
	unsigned int mtype = 0;
	unsigned int caps = CAP_OBSERVE;
	unsigned int proto = V2_VERSION;
	unsigned long long client_seq = 0;
	unsigned char nonce[16] = { 0 };
	int have_nonce = 0;
	int expect_error = 0;
	int expect_close = 0;
	int until_events = 0;
	long timeout_ms = 10000;
	unsigned long long inject_txn_id = 0;
	int inject_down_only = 0;
	int inject_begin_only = 0;
	int inject_no_commit = 0;
	int inject_crash_after_events = 0;
	int inject_bad_phase = !strcmp(mode, "inject-bad-phase");
	int inject_incomplete = !strcmp(mode, "inject-incomplete");
	int inject_pairs = 1;
	long inject_stay_ms = 0;
	int arb_dynamic = 0;
	int decision_action = -1;
	long decision_delay_ms = 0;
	int decision_no_reply = 0;
	int decision_crash = 0;
	int i;

	for (i = 3; i < argc; ++i)
	{
		if (!strcmp(argv[i], "--caps") && i + 1 < argc) caps = (unsigned int)strtoul(argv[++i], NULL, 0);
		else if (!strcmp(argv[i], "--proto") && i + 1 < argc) proto = (unsigned int)atoi(argv[++i]);
		else if (!strcmp(argv[i], "--seq") && i + 1 < argc) client_seq = strtoull(argv[++i], NULL, 0);
		else if (!strcmp(argv[i], "--expect-error")) expect_error = 1;
		else if (!strcmp(argv[i], "--expect-close")) expect_close = 1;
		else if (!strcmp(argv[i], "--until-events") && i + 1 < argc) until_events = atoi(argv[++i]);
		else if (!strcmp(argv[i], "--timeout-ms") && i + 1 < argc) timeout_ms = atol(argv[++i]);
		else if (!strcmp(argv[i], "--txn") && i + 1 < argc) inject_txn_id = strtoull(argv[++i], NULL, 0);
		else if (!strcmp(argv[i], "--down-only")) inject_down_only = 1;
		else if (!strcmp(argv[i], "--begin-only")) inject_begin_only = 1;
		else if (!strcmp(argv[i], "--no-commit")) inject_no_commit = 1;
		else if (!strcmp(argv[i], "--crash-after-events")) inject_crash_after_events = 1;
		else if (!strcmp(argv[i], "--pairs") && i + 1 < argc) inject_pairs = atoi(argv[++i]);
		else if (!strcmp(argv[i], "--stay") && i + 1 < argc) inject_stay_ms = atol(argv[++i]);
		else if (!strcmp(argv[i], "--dynamic")) arb_dynamic = 1;
		else if (!strcmp(argv[i], "--decision") && i + 1 < argc)
		{
			const char *v = argv[++i];
			decision_action = !strcmp(v, "suppress") ? 1 : 0;
		}
		else if (!strcmp(argv[i], "--decision-delay") && i + 1 < argc)
			decision_delay_ms = atol(argv[++i]);
		else if (!strcmp(argv[i], "--no-reply")) decision_no_reply = 1;
		else if (!strcmp(argv[i], "--crash-on-request")) decision_crash = 1;
		else if (!strcmp(argv[i], "--nonce") && i + 1 < argc)
		{
			const char *hex = argv[++i];
			int b;
			if (strlen(hex) == 32)
				for (b = 0; b < 16; ++b)
					nonce[b] = (unsigned char)strtoul(hex + (size_t)b * 2, NULL, 16);
			have_nonce = 1;
		}
	}

	if (!strcmp(mode, "health") || !strcmp(mode, "health-sub"))
	{
		if (send_hello(fd, proto, caps, 0, nonce, have_nonce) != 0) return 1;
		if (read_one_print(fd, &mtype, rp, &rp_len) != 1 || mtype != MSG_HELLO_ACK) return 1;
		print_ack(rp, rp_len);
		if (send_frame(fd, MSG_PING, 1, NULL, 0) != 0) return 1;
		if (read_one_print(fd, &mtype, rp, &rp_len) != 1 || mtype != MSG_PONG) return 1;
		if (read_one_print(fd, &mtype, rp, &rp_len) != 1 || mtype != MSG_BACKEND_HEALTH) return 1;
		print_health(rp, rp_len);
		if (!strcmp(mode, "health-sub"))
		{
			st_le32(payload, 0); /* empty but reconciled subscription */
			if (send_frame(fd, MSG_SUBSCRIBE, 2, payload, 4) != 0) return 1;
			int saw_ack = 0, saw_health = 0;
			for (int frame = 0; frame < 4 && !saw_health; ++frame)
			{
				if (read_one_print(fd, &mtype, rp, &rp_len) != 1) return 1;
				if (mtype == MSG_SUBSCRIBE_ACK)
				{
					printf("SUBSCRIBE_ACK ok=%u granted=%u\n", (unsigned)rp[0], ld_le32(rp + 1));
					saw_ack = 1;
				}
				else if (mtype == MSG_BACKEND_HEALTH)
				{
					print_health(rp, rp_len);
					saw_health = 1;
				}
			}
			return saw_ack && saw_health ? 0 : 1;
		}
		return 0;
	}

	if (!strcmp(mode, "hello"))
	{
		int r;
		if (send_hello(fd, proto, caps, client_seq, nonce, have_nonce) != 0) return 1;
		r = read_one_print(fd, &mtype, rp, &rp_len);
		if (r != 1) return expect_close ? 0 : 1;
		if (mtype == MSG_HELLO_ACK) { print_ack(rp, rp_len); return 0; }
		if (mtype == MSG_ERROR) { print_error(rp, rp_len); return expect_error ? 0 : 1; }
		return 1;
	}
	if (!strcmp(mode, "hello2"))
	{
		if (send_hello(fd, proto, caps, 0, nonce, have_nonce) != 0) return 1;
		if (read_one_print(fd, &mtype, rp, &rp_len) != 1 || mtype != MSG_HELLO_ACK) return 1;
		if (send_hello(fd, proto, caps, 1, nonce, have_nonce) != 0) return 1;
		if (read_one_print(fd, &mtype, rp, &rp_len) != 1) return 1;
		if (mtype != MSG_ERROR) { printf("EXPECTED_ERROR_GOT_TYPE=%u\n", mtype); return 1; }
		print_error(rp, rp_len);
		return ld_le32(rp) == ERR_DUPLICATE_HELLO ? 0 : 1;
	}
	if (!strcmp(mode, "nothello"))
	{
		st_le32(payload, 0); /* SUBSCRIBE count=0 as first frame */
		if (send_frame(fd, MSG_SUBSCRIBE, 0, payload, 4) != 0) return 1;
		if (read_one_print(fd, &mtype, rp, &rp_len) != 1) return 1;
		if (mtype != MSG_ERROR) { printf("EXPECTED_ERROR_GOT_TYPE=%u\n", mtype); return 1; }
		print_error(rp, rp_len);
		return ld_le32(rp) == ERR_NOT_HELLOED ? 0 : 1;
	}
	if (!strcmp(mode, "seqviol"))
	{
		if (send_hello(fd, proto, caps, 0, nonce, have_nonce) != 0) return 1;
		if (read_one_print(fd, &mtype, rp, &rp_len) != 1 || mtype != MSG_HELLO_ACK) return 1;
		if (send_frame(fd, MSG_PING, 0, NULL, 0) != 0) return 1;
		if (read_one_print(fd, &mtype, rp, &rp_len) != 1) return 1;
		if (mtype != MSG_ERROR) { printf("EXPECTED_ERROR_GOT_TYPE=%u\n", mtype); return 1; }
		print_error(rp, rp_len);
		return ld_le32(rp) == ERR_SEQUENCE_VIOLATION ? 0 : 1;
	}
	if (!strcmp(mode, "badmagic"))
	{
		unsigned char junk[40];
		int r;
		memset(junk, 'X', sizeof(junk));
		if (write_full(fd, junk, sizeof(junk)) != 0) return 1;
		/* The broker may send one ERROR before closing; either way the
		 * connection must be closed. */
		r = read_one_print(fd, &mtype, rp, &rp_len);
		if (r == 0) return 0;
		if (r == 1) r = read_one_print(fd, &mtype, rp, &rp_len);
		return r == 0 ? 0 : 1;
	}
	if (!strcmp(mode, "oversized"))
	{
		unsigned char frame[4 + V2_HEADER_LEN];
		unsigned char *p = frame;
		int r;
		st_le32(p, V2_MAGIC); p += 4;
		st_le16(p, V2_VERSION); p += 2;
		st_le16(p, V2_HEADER_LEN); p += 2;
		st_le32(p, 0xFFFFFFu); p += 4; /* message_len far beyond max */
		st_le16(p, MSG_HELLO); p += 2;
		st_le16(p, 0); p += 2;
		st_le64(p, 0); p += 8;
		st_le64(p, 0); p += 8;
		if (write_full(fd, frame, sizeof(frame)) != 0) return 1;
		r = read_one_print(fd, &mtype, rp, &rp_len);
		if (r == 0) return 0;
		if (r == 1) r = read_one_print(fd, &mtype, rp, &rp_len);
		return r == 0 ? 0 : 1;
	}
	if (!strcmp(mode, "badtype"))
	{
		if (send_hello(fd, proto, caps, 0, nonce, have_nonce) != 0) return 1;
		if (read_one_print(fd, &mtype, rp, &rp_len) != 1 || mtype != MSG_HELLO_ACK) return 1;
		if (send_frame(fd, 0xFFFFu, 1, NULL, 0) != 0) return 1;
		if (read_one_print(fd, &mtype, rp, &rp_len) != 1) return 1;
		if (mtype != MSG_ERROR) { printf("EXPECTED_ERROR_GOT_TYPE=%u\n", mtype); return 1; }
		print_error(rp, rp_len);
		return ld_le32(rp) == ERR_BAD_FRAME ? 0 : 1;
	}
	if (!strcmp(mode, "sub") || !strcmp(mode, "deny-sup"))
	{
		const char *rules;
		unsigned int codes[128];
		unsigned char sup[128];
		int count;
		unsigned char *p;
		if (mode[0] == 'd')
			caps = CAP_OBSERVE | CAP_SUPPRESS;
		if (send_hello(fd, proto, caps, 0, nonce, have_nonce) != 0) return 1;
		if (read_one_print(fd, &mtype, rp, &rp_len) != 1 || mtype != MSG_HELLO_ACK) return 1;
		print_ack(rp, rp_len);
		rules = mode[0] == 'd' ? "30:1" : (argc > 3 ? argv[3] : "");
		count = parse_rules(rules, codes, sup, 128);
		p = payload;
		st_le32(p, (unsigned int)count); p += 4;
		for (i = 0; i < count; ++i)
		{
			st_le32(p, codes[i]); p += 4;
			*p++ = sup[i];
		}
		if (send_frame(fd, MSG_SUBSCRIBE, 1, payload, 4 + (size_t)count * 5) != 0) return 1;
		if (read_one_print(fd, &mtype, rp, &rp_len) != 1) return 1;
		if (mtype == MSG_SUBSCRIBE_ACK)
		{
			printf("SUBSCRIBE_ACK ok=%u granted=%u\n", (unsigned)rp[0], ld_le32(rp + 1));
			return 0;
		}
		if (mtype == MSG_ERROR)
		{
			print_error(rp, rp_len);
			return (mode[0] == 'd' && ld_le32(rp) == ERR_CAPABILITY_DENIED) ? 0 : 1;
		}
		return 1;
	}
	if (!strcmp(mode, "watch"))
	{
		const char *rules = argc > 3 ? argv[3] : "";
		unsigned int codes[128];
		unsigned char sup[128];
		int count;
		unsigned char *p;
		int events = 0;
		if (send_hello(fd, proto, caps, 0, nonce, have_nonce) != 0) return 1;
		if (read_one_print(fd, &mtype, rp, &rp_len) != 1 || mtype != MSG_HELLO_ACK) return 1;
		print_ack(rp, rp_len);
		count = parse_rules(rules, codes, sup, 128);
		p = payload;
		st_le32(p, (unsigned int)count); p += 4;
		for (i = 0; i < count; ++i)
		{
			st_le32(p, codes[i]); p += 4;
			*p++ = sup[i];
		}
		if (send_frame(fd, MSG_SUBSCRIBE, 1, payload, 4 + (size_t)count * 5) != 0) return 1;
		while (timeout_ms > 0)
		{
			struct pollfd pfd = { fd, POLLIN, 0 };
			int pr = poll(&pfd, 1, 200);
			if (pr == 0) { timeout_ms -= 200; continue; }
			if (pr < 0) break;
			{
				int r = read_frame(fd, &mtype, rp, &rp_len);
				if (r == 0) { printf("CLOSED\n"); break; }
				if (r < 0) { printf("NO_RESPONSE\n"); break; }
			}
			switch (mtype)
			{
			case MSG_SUBSCRIBE_ACK:
				printf("SUBSCRIBE_ACK ok=%u granted=%u\n", (unsigned)rp[0], ld_le32(rp + 1));
				break;
			case MSG_EVENT:
				if (rp_len == 98)
				{
					print_event(rp);
					if (++events >= until_events && until_events)
					{
						printf("WATCH_END events=%d\n", events);
						close(fd);
						return 0;
					}
				}
				else
					printf("EVENT_SHORT len=%zu\n", rp_len);
				break;
			case MSG_DEVICE_ADDED:
				printf("DEVICE_ADDED id=%llu name=%.*s\n", ld_le64(rp)
					, (int)(rp_len > 10 ? ld_le16(rp + 8) : 0), (const char *)rp + 10);
				break;
			case MSG_DEVICE_REMOVED:
				printf("DEVICE_REMOVED id=%llu\n", ld_le64(rp));
				break;
			case MSG_BACKEND_DEGRADED:
				printf("BACKEND_DEGRADED\n");
				break;
			case MSG_PONG:
				printf("PONG\n");
				break;
			case MSG_ERROR:
				print_error(rp, rp_len);
				break;
			case MSG_ARB_REGISTER_ACK:
				print_arb_ack(rp, rp_len);
				break;
			case MSG_CONFLICT:
				print_conflict(rp, rp_len);
				break;
			case MSG_ARB_DECISION:
				print_decision(rp, rp_len);
				break;
			case MSG_BACKEND_HEALTH:
				print_health(rp, rp_len);
				break;
			default:
				printf("UNEXPECTED_TYPE=%u\n", mtype);
				break;
			}
			fflush(stdout);
		}
		printf("WATCH_END events=%d\n", events);
		fflush(stdout);
		return until_events ? (events >= until_events ? 0 : 1) : 0;
	}

	if (!strcmp(mode, "arb"))
	{
		/* arb CODE MODE REG_ID PRIORITY LEASE_MS REPLACEMENT_CODE
		 *     REPLACEMENT_LEVEL POLICY [--stay MS]
		 * MODE/POLICY are numeric INPUTD_V2_ARB_* ids. */
		if (argc < 11)
		{
			fprintf(stderr, "usage: %s SOCKET arb CODE MODE REG_ID PRIORITY LEASE_MS REPLACEMENT_CODE REPLACEMENT_LEVEL POLICY [--stay MS]\n", argv[0]);
			return 2;
		}
		unsigned int code = (unsigned int)strtoul(argv[3], NULL, 0);
		unsigned int arb_mode = (unsigned int)strtoul(argv[4], NULL, 0);
		unsigned long long reg_id = strtoull(argv[5], NULL, 0);
		int priority = atoi(argv[6]);
		unsigned int lease_ms = (unsigned int)strtoul(argv[7], NULL, 0);
		unsigned int replacement_code = (unsigned int)strtoul(argv[8], NULL, 0);
		int replacement_level = atoi(argv[9]);
		unsigned int policy = (unsigned int)strtoul(argv[10], NULL, 0);
		caps = CAP_OBSERVE | CAP_SUPPRESS | CAP_EXCLUSIVE | CAP_INJECT;
		if (send_hello(fd, proto, caps, 0, nonce, have_nonce) != 0) return 1;
		if (read_one_print(fd, &mtype, rp, &rp_len) != 1 || mtype != MSG_HELLO_ACK) return 1;
		print_ack(rp, rp_len);
		unsigned char ap[30];
		memset(ap, 0, sizeof(ap));
		st_le64(ap, reg_id);
		st_le32(ap + 8, code);
		ap[12] = (unsigned char)arb_mode;
		ap[13] = (unsigned char)policy;
		st_le16(ap + 14, (unsigned int)(unsigned short)priority);
		st_le16(ap + 16, 0); /* input_level policy metadata */
		st_le16(ap + 18, (unsigned int)(unsigned short)replacement_level);
		st_le32(ap + 20, lease_ms);
		st_le32(ap + 24, replacement_code);
		st_le16(ap + 28, arb_dynamic ? 1u : 0u);
		if (send_frame(fd, MSG_ARB_REGISTER, 1, ap, sizeof(ap)) != 0) return 1;
		if (read_one_print(fd, &mtype, rp, &rp_len) != 1
			|| mtype != MSG_ARB_REGISTER_ACK) return 1;
		print_arb_ack(rp, rp_len);
		unsigned int status = rp_len >= 9 ? rp[8] : ARB_BAD_FRAME;
		long stay_ms = inject_stay_ms;
		unsigned long long arb_seq = 2;
		/* Conflicts are followed by a CONFLICT sideband.  A granted rule
		 * stays connected for --stay so the broker retains ownership. */
		if (status == ARB_CONFLICTED)
		{
			if (read_one_print(fd, &mtype, rp, &rp_len) == 1
				&& mtype == MSG_CONFLICT)
				print_conflict(rp, rp_len);
			return 0;
		}
		if (status != ARB_GRANTED && status != ARB_REFRESHED)
			return 1;
		while (stay_ms > 0)
		{
			struct pollfd pfd = { fd, POLLIN, 0 };
			int step = stay_ms > 200 ? 200 : (int)stay_ms;
			int pr = poll(&pfd, 1, step);
			stay_ms -= step;
			if (pr <= 0) continue;
			int r = read_frame(fd, &mtype, rp, &rp_len);
			if (r <= 0) break;
			if (mtype == MSG_CONFLICT) print_conflict(rp, rp_len);
			else if (mtype == MSG_DECISION_REQUEST && rp_len == 46)
			{
				unsigned long long event_seq = ld_le64(rp);
				unsigned long long source_txn = ld_le64(rp + 8);
				unsigned long long request_reg = ld_le64(rp + 16);
				unsigned int request_code = ld_le32(rp + 24);
				unsigned int request_value = rp[28];
				unsigned long long deadline = ld_le64(rp + 30);
				unsigned long long acceptance = ld_le64(rp + 38);
				printf("DECISION_REQUEST seq=%llu txn=%llu reg=%llu code=%u value=%u deadline=%llu acceptance=%llu\n",
					event_seq, source_txn, request_reg, request_code,
					request_value, deadline, acceptance);
				fflush(stdout);
				if (decision_crash)
					_exit(98);
				if (!decision_no_reply)
				{
					if (decision_delay_ms > 0)
						usleep((useconds_t)decision_delay_ms * 1000);
					unsigned char reply[33];
					memset(reply, 0, sizeof(reply));
					st_le64(reply, event_seq);
					st_le64(reply + 8, source_txn);
					st_le64(reply + 16, request_reg);
					st_le64(reply + 24, acceptance);
					reply[32] = (unsigned char)(decision_action == 1 ? 1 : 0);
					if (send_frame(fd, MSG_DECISION_REPLY, arb_seq++, reply,
							sizeof(reply)) != 0) return 1;
					printf("DECISION_REPLY seq=%llu action=%s\n", event_seq,
						decision_action == 1 ? "suppress" : "pass");
					fflush(stdout);
				}
			}
			else if (mtype == MSG_ARB_DECISION) print_decision(rp, rp_len);
			else if (mtype == MSG_EVENT && rp_len == 98) print_event(rp);
			else if (mtype == MSG_ARB_REGISTER_ACK) print_arb_ack(rp, rp_len);
			else if (mtype == MSG_BACKEND_HEALTH) print_health(rp, rp_len);
			else if (mtype == MSG_ERROR) print_error(rp, rp_len);
		}
		close(fd);
		return 0;
	}

	if (!strcmp(mode, "inject") || !strcmp(mode, "inject-event")
		|| inject_bad_phase || inject_incomplete)
	{
		int is_single = !strcmp(mode, "inject-event");
		int code, level;
		int p;
		unsigned long long seq = 1;
		if (argc < 5)
		{
			fprintf(stderr, "usage: %s SOCKET %s CODE LEVEL [--txn N] [--down-only] [--begin-only] [--pairs N] [--no-commit] [--stay MS]\n",
				argv[0], mode);
			return 2;
		}
		code = atoi(argv[3]);
		level = atoi(argv[4]);
		if (!inject_txn_id)
			inject_txn_id = 0xA11CE000ULL + (unsigned long long)getpid();
		caps = CAP_OBSERVE | CAP_INJECT;
		if (send_hello(fd, proto, caps, 0, nonce, have_nonce) != 0) return 1;
		if (read_one_print(fd, &mtype, rp, &rp_len) != 1 || mtype != MSG_HELLO_ACK) return 1;
		print_ack(rp, rp_len);

		/* key payload: 16 bytes LE (code, phase, value) */
		if (is_single)
		{
			/* INJECT_EVENT without BEGIN: the broker must answer STALE. */
			unsigned char ep[24];
			st_le64(ep, inject_txn_id);
			key_payload_fill((unsigned int)code, 1, ep + 8);
			if (send_frame(fd, MSG_INJECT_EVENT, seq++, ep, sizeof(ep)) != 0) return 1;
			if (read_one_print(fd, &mtype, rp, &rp_len) != 1) return 1;
			if (mtype != MSG_INJECT_ACK) { printf("EXPECTED_INJECT_ACK_GOT=%u\n", mtype); return 1; }
			print_inject_ack(rp, rp_len);
			return (unsigned)rp[8] == INJECT_STALE ? 0 : 1;
		}

		{
			unsigned int event_count = (unsigned int)inject_pairs * (inject_down_only ? 1u : 2u);
			unsigned char bp[26];
			st_le64(bp, inject_txn_id);
			st_le16(bp + 8, (unsigned int)(unsigned short)level);
			st_le16(bp + 10, 0);
			st_le16(bp + 12, 0); /* ttl: broker default */
			st_le32(bp + 14, event_count);
			st_le64(bp + 18, 0); /* parent */
			if (send_frame(fd, MSG_INJECT_BEGIN, seq++, bp, sizeof(bp)) != 0) return 1;
			if (read_one_print(fd, &mtype, rp, &rp_len) != 1) return 1;
			if (mtype != MSG_INJECT_ACK) { printf("EXPECTED_INJECT_ACK_GOT=%u\n", mtype); return 1; }
			print_inject_ack(rp, rp_len);
			if ((unsigned)rp[8] != INJECT_OK_BEGIN) return 1;
			if (inject_bad_phase)
			{
				unsigned char ep[24];
				st_le64(ep, inject_txn_id);
				memset(ep + 8, 0, 16);
				st_le32(ep + 8, (unsigned int)code);
				ep[20] = 0; /* DOWN phase with value=0 is malformed. */
				ep[21] = 0;
				if (send_frame(fd, MSG_INJECT_EVENT, seq++, ep, sizeof(ep)) != 0) return 1;
				if (read_one_print(fd, &mtype, rp, &rp_len) != 1) return 1;
				print_inject_ack(rp, rp_len);
				return (unsigned)rp[8] == INJECT_BAD_FRAME ? 0 : 1;
			}
			if (inject_incomplete)
			{
				unsigned char ep[24];
				st_le64(ep, inject_txn_id);
				key_payload_fill((unsigned int)code, 1, ep + 8);
				if (send_frame(fd, MSG_INJECT_EVENT, seq++, ep, sizeof(ep)) != 0) return 1;
				if (read_one_print(fd, &mtype, rp, &rp_len) != 1
					|| mtype != MSG_INJECT_ACK || (unsigned)rp[8] != INJECT_OK_EVENT)
					return 1;
				unsigned char cp[8];
				st_le64(cp, inject_txn_id);
				if (send_frame(fd, MSG_INJECT_COMMIT, seq++, cp, sizeof(cp)) != 0) return 1;
				if (read_one_print(fd, &mtype, rp, &rp_len) != 1) return 1;
				print_inject_ack(rp, rp_len);
				return (unsigned)rp[8] == INJECT_BAD_FRAME ? 0 : 1;
			}

			if (!inject_begin_only)
				for (p = 0; p < inject_pairs; ++p)
				{
					unsigned char ep[24];
					st_le64(ep, inject_txn_id);
					key_payload_fill((unsigned int)code, 1, ep + 8);
					if (send_frame(fd, MSG_INJECT_EVENT, seq++, ep, sizeof(ep)) != 0) return 1;
					if (read_one_print(fd, &mtype, rp, &rp_len) != 1) return 1;
					if (mtype != MSG_INJECT_ACK) { printf("EXPECTED_INJECT_ACK_GOT=%u\n", mtype); return 1; }
					print_inject_ack(rp, rp_len);
					if ((unsigned)rp[8] != INJECT_OK_EVENT) return 1;
					if (!inject_down_only)
					{
						key_payload_fill((unsigned int)code, 0, ep + 8);
						if (send_frame(fd, MSG_INJECT_EVENT, seq++, ep, sizeof(ep)) != 0) return 1;
						if (read_one_print(fd, &mtype, rp, &rp_len) != 1) return 1;
						if (mtype != MSG_INJECT_ACK) { printf("EXPECTED_INJECT_ACK_GOT=%u\n", mtype); return 1; }
						print_inject_ack(rp, rp_len);
						if ((unsigned)rp[8] != INJECT_OK_EVENT) return 1;
					}
				}

			if (inject_crash_after_events)
			{
				fflush(stdout);
				_exit(99); /* no ABORT/COMMIT: broker observes owner crash */
			}
			if (inject_no_commit)
			{
				if (inject_stay_ms > 0)
					usleep((useconds_t)inject_stay_ms * 1000);
				for (;;)
					pause(); /* external kill; broker must abort+balance */
			}
			if (inject_stay_ms > 0)
				usleep((useconds_t)inject_stay_ms * 1000);
			{
				unsigned char cp[8];
				st_le64(cp, inject_txn_id);
				if (send_frame(fd, MSG_INJECT_COMMIT, seq++, cp, sizeof(cp)) != 0) return 1;
				if (read_one_print(fd, &mtype, rp, &rp_len) != 1) return 1;
				if (mtype != MSG_INJECT_ACK) { printf("EXPECTED_INJECT_ACK_GOT=%u\n", mtype); return 1; }
				print_inject_ack(rp, rp_len);
				return (unsigned)rp[8] == INJECT_OK_COMMIT ? 0 : 1;
			}
		}
	}

	fprintf(stderr, "v2_probe: unknown mode %s\n", mode);
	close(fd);
	return 2;
}
