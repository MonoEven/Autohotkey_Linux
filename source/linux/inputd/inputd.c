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
 * Build:  cc -O2 -Wall -o ahk-inputd inputd.c   (no X11/AHK dependency)
 * Usage:  ahk-inputd [--socket PATH]
 *         AHK_INPUT_BACKEND=evdev scripts auto-connect to this socket.
 */
#define _GNU_SOURCE /* ppoll */
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

#define PROTO_VERSION 1u
#define PANIC_TIMEOUT_MS 1500
#define WATCHDOG_MS 2000
#define MAX_CLIENTS 32
#define MAX_DEVICES 64
#define PANIC_BACKSPACE 14
#define PANIC_ESCAPE 1
#define PANIC_ENTER 28

/* ---- protocol v1 (length-prefixed binary frames) ------------------------ */

#define C2S_HELLO 1u
#define C2S_SUBSCRIBE 2u
#define C2S_UNSUBSCRIBE 3u
#define C2S_PING 4u

#define S2C_EVENT 1u
#define S2C_ACK 2u
#define S2C_PONG 3u

struct rule { unsigned int code; unsigned char suppress; };

struct client {
	int fd;
	struct rule rules[128];
	int rule_count;
	char dead;
};

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
static int sVerbose = 0;
static volatile sig_atomic_t sQuit = 0;

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

static void logmsg(const char *fmt, ...)
{
	if (!sVerbose) return;
	va_list ap; va_start(ap, fmt);
	fprintf(stderr, "[inputd] ");
	vfprintf(stderr, fmt, ap);
	fprintf(stderr, "\n");
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

static int open_uinput_replay(void)
{
	int fd = open("/dev/uinput", O_WRONLY | O_NONBLOCK);
	if (fd < 0) { fprintf(stderr, "[inputd] /dev/uinput: %s\n", strerror(errno)); return -1; }
	ioctl(fd, UI_SET_EVBIT, EV_KEY);
	ioctl(fd, UI_SET_EVBIT, EV_SYN);
	for (int c = 1; c < KEY_MAX; ++c)
		ioctl(fd, UI_SET_KEYBIT, c);
	struct uinput_setup setup;
	memset(&setup, 0, sizeof(setup));
	setup.id.bustype = BUS_USB;
	setup.id.vendor  = 0x0FAC; /* keyd vendor id, libinput quirk friendly */
	setup.id.product = 0xABCD;
	setup.id.version = 1;
	strncpy(setup.name, "ahk-inputd virtual keyboard", UINPUT_MAX_NAME_SIZE);
	if (ioctl(fd, UI_DEV_SETUP, &setup) != 0 || ioctl(fd, UI_DEV_CREATE) != 0)
	{
		fprintf(stderr, "[inputd] uinput create: %s\n", strerror(errno));
		close(fd);
		return -1;
	}
	return fd;
}

static void replay_key(unsigned int code, int value)
{
	if (sUinputFd < 0) return;
	struct input_event e;
	memset(&e, 0, sizeof(e));
	e.type = EV_KEY; e.code = (__u16)code; e.value = (__s32)value;
	ssize_t ignored = write(sUinputFd, &e, sizeof(e));
	(void)ignored;
	memset(&e, 0, sizeof(e));
	e.type = EV_SYN; e.code = SYN_REPORT;
	ignored = write(sUinputFd, &e, sizeof(e));
	(void)ignored;
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
	frame[0] = S2C_EVENT;
	memcpy(frame + 1, &code, 4);
	frame[5] = (unsigned char)value;
	memcpy(frame + 6, &ts, 8);
	if (write_full(c->fd, frame, sizeof(frame)) != 0)
		c->dead = 1;
}

static void send_ack(struct client *c, unsigned char ok, unsigned int detail)
{
	unsigned char frame[1 + 1 + 4];
	frame[0] = S2C_ACK; frame[1] = ok;
	memcpy(frame + 2, &detail, 4);
	if (write_full(c->fd, frame, sizeof(frame)) != 0)
		c->dead = 1;
}

static void send_pong(struct client *c)
{
	unsigned char frame[1];
	frame[0] = S2C_PONG;
	if (write_full(c->fd, frame, sizeof(frame)) != 0)
		c->dead = 1;
}

/* ---- client handling ----------------------------------------------------- */

static void close_client(struct client *c)
{
	if (c->fd >= 0) close(c->fd);
	c->fd = -1;
	c->rule_count = 0;
	c->dead = 0;
	logmsg("client disconnected, rules dropped", 0);
}

static int accept_client(void)
{
	struct sockaddr_un addr;
	socklen_t len = sizeof(addr);
	int fd = accept(sListenFd, (struct sockaddr *)&addr, &len);
	if (fd < 0) return -1;
	int flags = fcntl(fd, F_GETFL, 0);
	fcntl(fd, F_SETFL, flags | O_NONBLOCK);
	for (int i = 0; i < MAX_CLIENTS; ++i)
		if (sClients[i].fd < 0)
		{
			sClients[i].fd = fd;
			sClients[i].rule_count = 0;
			logmsg("client %d connected", fd);
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
	case C2S_HELLO:
	{
		if (n != 1 + 4) { c->dead = 1; return; }
		unsigned int ver;
		memcpy(&ver, cmd + 1, 4);
		send_ack(c, ver == PROTO_VERSION ? 1 : 0, PROTO_VERSION);
		break;
	}
	case C2S_SUBSCRIBE:
	{
		if (n < 1 + 4) { c->dead = 1; return; }
		unsigned int count;
		memcpy(&count, cmd + 1, 4);
		if (count > 128 || n != 1 + 4 + (size_t)count * 5) { c->dead = 1; return; }
		c->rule_count = 0;
		for (unsigned int i = 0; i < count; ++i)
		{
			const unsigned char *r = cmd + 5 + (size_t)i * 5;
			unsigned int code;
			memcpy(&code, r, 4);
			c->rules[c->rule_count].code = code;
			c->rules[c->rule_count].suppress = r[4];
			c->rule_count++;
		}
		logmsg("client %d subscribed %d rule(s)", c->fd, c->rule_count);
		send_ack(c, 1, (unsigned int)c->rule_count);
		break;
	}
	case C2S_UNSUBSCRIBE:
		c->rule_count = 0;
		send_ack(c, 1, 0);
		break;
	case C2S_PING:
		send_pong(c);
		break;
	default:
		c->dead = 1;
	}
}

/* ---- device scanning ------------------------------------------------------ */

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

static void scan_devices(void)
{
	DIR *dir = opendir("/dev/input");
	if (!dir) { fprintf(stderr, "[inputd] /dev/input: %s\n", strerror(errno)); return; }
	struct dirent *ent;
	while ((ent = readdir(dir)) != NULL)
	{
		if (strncmp(ent->d_name, "event", 5) != 0)
			continue;
		int already = 0;
		for (int i = 0; i < sDevCount && !already; ++i)
			if (sGrabbedNames[i][0] && !strcmp(sGrabbedNames[i], ent->d_name))
				already = 1;
		if (already)
			continue;
		char path[128];
		snprintf(path, sizeof(path), "/dev/input/%s", ent->d_name);
		int fd = open(path, O_RDONLY | O_NONBLOCK);
		if (fd < 0) continue;
		if (is_self_device(fd)) { close(fd); continue; }
		if (!is_keyboard(fd)) { close(fd); continue; }
		if (sDevCount < MAX_DEVICES)
		{
			if (ioctl(fd, EVIOCGRAB, 1) == 0)
			{
				sDevFds[sDevCount] = fd;
				strncpy(sGrabbedNames[sDevCount], ent->d_name, sizeof(sGrabbedNames[0]) - 1);
				sGrabbedNames[sDevCount][sizeof(sGrabbedNames[0]) - 1] = '\0';
				sDevCount++;
				sAnyGrabbed = 1;
				logmsg("grabbed %s", path);
			}
			else
			{
				fprintf(stderr, "[inputd] EVIOCGRAB %s: %s (running as root?)\n", path, strerror(errno));
				close(fd);
			}
		}
		else
			close(fd);
	}
	closedir(dir);
}

/* ---- main ---------------------------------------------------------------- */

static void on_sig(int s)
{
	if (s == SIGALRM)
	{
		/* Watchdog: main loop was stuck.  Fail open immediately. */
		fprintf(stderr, "[inputd] watchdog: main loop stuck, releasing grabs\n");
		release_all_grabs();
	}
	else if (s == SIGTERM || s == SIGINT)
		sQuit = 1;
}

int main(int argc, char **argv)
{
	const char *socket_path = NULL;
	for (int i = 1; i < argc; ++i)
	{
		if (!strcmp(argv[i], "--socket") && i + 1 < argc)
			socket_path = argv[++i];
		else if (!strcmp(argv[i], "-v"))
			sVerbose = 1;
		else
		{
			fprintf(stderr, "usage: %s [--socket PATH] [-v]\n", argv[0]);
			return 2;
		}
	}
	if (!socket_path)
	{
		const char *rt = getenv("XDG_RUNTIME_DIR");
		static char def[256];
		snprintf(def, sizeof(def), "%s/ahk-inputd.sock", (rt && *rt) ? rt : "/tmp");
		socket_path = def;
	}

	/* single instance */
	char lock_path[320];
	snprintf(lock_path, sizeof(lock_path), "%s.lock", socket_path);
	int lock_fd = open(lock_path, O_CREAT | O_RDWR, 0600);
	if (lock_fd < 0 || flock(lock_fd, LOCK_EX | LOCK_NB) != 0)
	{
		fprintf(stderr, "[inputd] another instance holds %s\n", lock_path);
		return 1;
	}
	unlink(socket_path);
	sListenFd = socket(AF_UNIX, SOCK_STREAM, 0);
	struct sockaddr_un addr;
	memset(&addr, 0, sizeof(addr));
	addr.sun_family = AF_UNIX;
	snprintf(addr.sun_path, sizeof(addr.sun_path), "%s", socket_path);
	if (sListenFd < 0 || bind(sListenFd, (struct sockaddr *)&addr, sizeof(addr)) != 0
		|| listen(sListenFd, 8) != 0)
	{
		fprintf(stderr, "[inputd] bind/listen %s: %s\n", socket_path, strerror(errno));
		return 1;
	}
	chmod(socket_path, 0666); /* any user on the machine may connect */
	for (int i = 0; i < MAX_CLIENTS; ++i)
	{
		sClients[i].fd = -1;
		sClients[i].rules[0].code = 0;
	}
	for (int i = 0; i < MAX_DEVICES; ++i) sDevFds[i] = -1;
	memset(sGrabbedNames, 0, sizeof(sGrabbedNames));

	signal(SIGTERM, on_sig);
	signal(SIGINT, on_sig);
	signal(SIGALRM, on_sig); /* watchdog: release grabs, keep serving */
	signal(SIGPIPE, SIG_IGN);

	scan_devices();
	sUinputFd = open_uinput_replay();
	fprintf(stderr, "[inputd] ready on %s (%d keyboard(s) grabbed)\n", socket_path, sDevCount);

	struct pollfd pfds[1 + MAX_DEVICES + MAX_CLIENTS];
	struct timespec timeout = { 0, 200 * 1000 * 1000 };
	long long next_rescan = now_ms() + 1000;
	while (!sQuit)
	{
		int n = 0;
		pfds[n].fd = sListenFd; pfds[n].events = POLLIN; ++n;
		for (int i = 0; i < sDevCount; ++i)
			if (sDevFds[i] >= 0)
			{
				pfds[n].fd = sDevFds[i]; pfds[n].events = POLLIN; ++n;
			}
		for (int i = 0; i < MAX_CLIENTS; ++i)
			if (sClients[i].fd >= 0)
			{
				pfds[n].fd = sClients[i].fd; pfds[n].events = POLLIN; ++n;
			}
		int pr = ppoll(pfds, (nfds_t)n, &timeout, NULL);
		/* watchdog heartbeat: returning to this point proves liveness */
		alarm(2);
		/* pick up uinput/test keyboards hot-plugged after startup */
		long long now = now_ms();
		if (now >= next_rescan)
		{
			next_rescan = now + 1000;
			scan_devices();
		}
		if (pr < 0)
		{
			if (errno == EINTR) continue;
			break;
		}
		int idx = 0;
		if (pfds[idx].revents & POLLIN)
			(void)accept_client();
		++idx; /* listen slot is always present, with or without events */
		for (int i = 0; i < sDevCount; ++i, ++idx)
			if (sDevFds[i] >= 0 && (pfds[idx].revents & POLLIN))
			{
				struct input_event ev;
				for (;;)
				{
					ssize_t rd = read(sDevFds[i], &ev, sizeof(ev));
					if (rd != (ssize_t)sizeof(ev)) break;
					if (ev.type != EV_KEY) continue;
					if (panic_step((unsigned int)ev.code, ev.value != 0))
						continue;
					if (sPanicked) continue; /* fail-open: pass through */
					/* distribute to subscribed clients first */
					int any_suppress = 0;
					for (int c = 0; c < MAX_CLIENTS; ++c)
					{
						struct client *cl = &sClients[c];
						if (cl->fd < 0 || cl->dead) continue;
						for (int r = 0; r < cl->rule_count; ++r)
							if (cl->rules[r].code == (unsigned int)ev.code)
							{
								if (cl->rules[r].suppress)
									any_suppress = 1;
								send_event_to(cl, (unsigned int)ev.code, ev.value, now_us());
								break;
							}
					}
					if (!any_suppress)
						replay_key((unsigned int)ev.code, ev.value);
				}
			}
		for (int i = 0; i < MAX_CLIENTS; ++i, ++idx)
		{
			struct client *cl = &sClients[i];
			if (cl->fd >= 0 && (pfds[idx].revents & (POLLIN | POLLHUP | POLLERR)))
			{
				/* read header + one frame: simple bounded drain */
				unsigned char hdr[4];
				ssize_t got = read(cl->fd, hdr, 4);
				if (got <= 0)
				{
					close_client(cl);
					continue;
				}
				unsigned int len;
				memcpy(&len, hdr, 4);
				if (len > 65536) { close_client(cl); continue; }
				unsigned char *frame = malloc(4 + len);
				if (!frame) { close_client(cl); continue; }
				memcpy(frame, hdr, 4);
				size_t have = (size_t)got;
				while (have < 4 + len)
				{
					ssize_t rr = read(cl->fd, frame + have, 4 + len - have);
					if (rr <= 0)
					{
						if (rr == 0 || (errno != EAGAIN && errno != EINTR))
						{
							free(frame);
							close_client(cl);
							goto next_client;
						}
						usleep(1000);
						continue;
					}
					have += (size_t)rr;
				}
				handle_client_cmd(cl, frame + 4, len);
				free(frame);
			next_client:;
			}
		}
		/* drop clients that died mid-write */
		for (int i = 0; i < MAX_CLIENTS; ++i)
			if (sClients[i].fd >= 0 && sClients[i].dead)
				close_client(&sClients[i]);
	}
	release_all_grabs();
	close(sListenFd);
	unlink(socket_path);
	if (sUinputFd >= 0) { ioctl(sUinputFd, UI_DEV_DESTROY); close(sUinputFd); }
	for (int i = 0; i < MAX_CLIENTS; ++i)
		if (sClients[i].fd >= 0) close_client(&sClients[i]);
	fprintf(stderr, "[inputd] exited\n");
	return 0;
}
