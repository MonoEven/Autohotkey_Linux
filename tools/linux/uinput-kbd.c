/* uinput-kbd.c -- test key injector for the evdev/uinput lanes.
 *
 * Creates a virtual keyboard (named "test virt keyboard", deliberately
 * WITHOUT "AHK" in the name so the evdev capture lane treats it as a
 * physical keyboard) and sends EV_KEY events.
 *
 * Usage:
 *   uinput-kbd <evcode> [down|up|tap] [evcode ...]   (default: tap)
 *   uinput-kbd --hold <evcode> [down]  # keep the device alive afterwards
 *
 *   uinput-kbd 63 tap        # F12 press+release (63=KEY_F12)
 *   uinput-kbd 29 down 50 down 50 up 29 up   # Ctrl+N (29=LEFTCTRL, 50=N)
 *   uinput-kbd --hold 88 down # F12 held; device persists until SIGTERM
 *
 * evcodes: see /usr/include/linux/input-event-codes.h.  With --hold the
 * virtual device stays alive (for a capture lane that rescans /dev/input),
 * until SIGINT/SIGTERM.
 */
#include <linux/uinput.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <stdio.h>
#include <signal.h>

static volatile sig_atomic_t g_hold = 1;

static void write_ev(int fd, int type, int code, int val)
{
	struct input_event e;
	memset(&e, 0, sizeof(e));
	e.type  = (__u16)type;
	e.code  = (__u16)code;
	e.value = (__s32)val;
	if (write(fd, &e, sizeof(e)) != (ssize_t)sizeof(e))
	{
		fprintf(stderr, "uinput-kbd: write failed: %s\n", strerror(errno));
		exit(1);
	}
	memset(&e, 0, sizeof(e));
	e.type  = EV_SYN;
	e.code  = SYN_REPORT;
	if (write(fd, &e, sizeof(e)) != (ssize_t)sizeof(e))
	{
		fprintf(stderr, "uinput-kbd: SYN write failed: %s\n", strerror(errno));
		exit(1);
	}
}

static void on_sig(int s) { (void)s; g_hold = 0; }

int main(int argc, char **argv)
{
	int hold = 0;
	int delay_ms = 0;
	int argi = 1;
	while (argi < argc && argv[argi][0] == '-')
	{
		if (strcmp(argv[argi], "--hold") == 0) { hold = 1; ++argi; }
		else if (strcmp(argv[argi], "--delay-ms") == 0 && argi + 1 < argc) { delay_ms = atoi(argv[argi + 1]); argi += 2; }
		else { fprintf(stderr, "uinput-kbd: unknown option %s\n", argv[argi]); return 2; }
	}
	if (argc <= argi)
	{
		fprintf(stderr, "usage: %s [--hold] [--delay-ms N] <evcode> [down|up|tap] [evcode ...]\n", argv[0]);
		return 2;
	}
	int fd = open("/dev/uinput", O_WRONLY | O_NONBLOCK);
	if (fd < 0)
	{
		fprintf(stderr, "uinput-kbd: open /dev/uinput: %s\n", strerror(errno));
		return 1;
	}
	ioctl(fd, UI_SET_EVBIT, EV_KEY);
	ioctl(fd, UI_SET_EVBIT, EV_SYN);
	for (int c = 1; c < KEY_MAX; ++c)
		ioctl(fd, UI_SET_KEYBIT, c);
	struct uinput_setup setup;
	memset(&setup, 0, sizeof(setup));
	setup.id.bustype = BUS_USB;
	setup.id.vendor  = 0x1234;
	setup.id.product = 0xab01;
	setup.id.version = 1;
	strncpy(setup.name, "test virt keyboard", UINPUT_MAX_NAME_SIZE);
	if (ioctl(fd, UI_DEV_SETUP, &setup) != 0
		|| ioctl(fd, UI_DEV_CREATE) != 0)
	{
		fprintf(stderr, "uinput-kbd: device create failed: %s\n", strerror(errno));
		close(fd);
		return 1;
	}
	usleep(150000); /* give the compositor a moment to see the device */
	if (delay_ms > 0)
		usleep((useconds_t)delay_ms * 1000); /* let a capture lane rescan+grab first */

	for (int i = argi; i < argc; i += 2)
	{
		int code = atoi(argv[i]);
		const char *act = (i + 1 < argc) ? argv[i + 1] : "tap";
		if (strcmp(act, "down") == 0)
			write_ev(fd, EV_KEY, code, 1);
		else if (strcmp(act, "up") == 0)
			write_ev(fd, EV_KEY, code, 0);
		else if (strcmp(act, "tap") == 0)
		{
			write_ev(fd, EV_KEY, code, 1);
			write_ev(fd, EV_KEY, code, 0);
		}
		else
		{
			fprintf(stderr, "uinput-kbd: bad action '%s'\n", act);
			close(fd);
			return 2;
		}
	}

	if (hold)
	{
		signal(SIGINT, on_sig);
		signal(SIGTERM, on_sig);
		while (g_hold)
			usleep(200000);
	}
	usleep(150000);
	ioctl(fd, UI_DEV_DESTROY);
	close(fd);
	return 0;
}