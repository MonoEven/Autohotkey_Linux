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

#define INPUTD_PROTO_VERSION 1u
#define PANIC_TIMEOUT_MS 1500
#define ACTIVATED_IDLE_EXIT_MS 5000
#define MAX_CLIENTS 32
#define MAX_DEVICES 64
#define PANIC_BACKSPACE 14
#define PANIC_ESCAPE 1
#define PANIC_ENTER 28

/* ---- protocol v1 (see inputd_proto.h) ------------------------------------ */

struct rule { unsigned int code; unsigned char suppress; };

struct client {
	int fd;
	pid_t pid;
	uid_t uid;
	gid_t gid;
	unsigned char rx[INPUTD_MAX_FRAME];
	size_t rx_used;
	struct rule rules[INPUTD_MAX_RULES];
	int rule_count;
	char hello_ok;
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

/* ---- client handling ----------------------------------------------------- */

static void close_client(struct client *c)
{
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

static void drain_client(struct client *c)
{
	/* One authorized client must not monopolize the grab/replay loop by keeping
	 * its stream perpetually readable. Four maximum frames per poll turn is
	 * enough for normal rule updates while preserving fairness. */
	size_t read_budget = INPUTD_MAX_FRAME * 4u;
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

static void scan_devices(void)
{
	if (sPanicked)
		return;
	DIR *dir = opendir("/dev/input");
	if (!dir) { fprintf(stderr, "[inputd] /dev/input: %s\n", strerror(errno)); return; }
	struct dirent *ent;
	while ((ent = readdir(dir)) != NULL)
	{
		if (sPanicked)
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
		if (is_self_device(fd)) { close(fd); continue; }
		if (!is_keyboard(fd)) { close(fd); continue; }
		if (sDevCount < MAX_DEVICES)
		{
			/* Publish the fd before EVIOCGRAB so SIGALRM can always close a
			 * just-grabbed device even if it interrupts this ioctl. */
			int slot = sDevCount++;
			sDevFds[slot] = fd;
			memcpy(sGrabbedNames[slot], ent->d_name, device_name_len + 1);
			if (!sPanicked && ioctl(fd, EVIOCGRAB, 1) == 0 && !sPanicked)
			{
				sAnyGrabbed = 1;
				logmsg("grabbed %s", path);
			}
			else
			{
				if (!sPanicked)
					fprintf(stderr, "[inputd] EVIOCGRAB %s: %s (running as root?)\n", path, strerror(errno));
				close(fd); // EBADF is harmless if SIGALRM already closed it.
				sDevFds[slot] = -1;
				sGrabbedNames[slot][0] = '\0';
			}
		}
		else
			close(fd);
	}
	closedir(dir);
	prune_removed_devices();
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
		}
		++write_index;
	}
	for (int i = write_index; i < sDevCount; ++i)
	{
		sDevFds[i] = -1;
		sGrabbedNames[i][0] = '\0';
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
	for (int i = 0; i < MAX_CLIENTS; ++i)
	{
		sClients[i].fd = -1;
		sClients[i].pid = 0;
		sClients[i].uid = 0;
		sClients[i].gid = 0;
		sClients[i].rx_used = 0;
		sClients[i].hello_ok = 0;
		sClients[i].rules[0].code = 0;
	}
	for (int i = 0; i < MAX_DEVICES; ++i) sDevFds[i] = -1;
	memset(sGrabbedNames, 0, sizeof(sGrabbedNames));

	signal(SIGTERM, on_sig);
	signal(SIGINT, on_sig);
	signal(SIGALRM, on_sig); /* watchdog: release grabs, keep serving */
	signal(SIGPIPE, SIG_IGN);

	if (!protocol_only)
	{
		scan_devices();
		sUinputFd = open_uinput_replay();
	}
	fprintf(stderr, "[inputd] ready on %s (%d keyboard(s) grabbed%s)\n",
		socket_path, sDevCount, protocol_only ? ", protocol-only" : "");

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
			if (remove_device)
			{
				ioctl(sDevFds[i], EVIOCGRAB, 0);
				fprintf(stderr, "[inputd] hot-remove %s\n", sGrabbedNames[i]);
				close(sDevFds[i]);
				sDevFds[i] = -1;
				sGrabbedNames[i][0] = '\0';
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
