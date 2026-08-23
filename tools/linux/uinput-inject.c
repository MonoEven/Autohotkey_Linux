/* uinput-inject.c -- persistent uinput test keyboard with a command file.
 * Creates a virtual keyboard named "inj test keyboard" (no "AHK" in the name,
 * so the evdev capture lane treats it as physical) and stays alive; when the
 * command file (arg 1, default /tmp/uinj_cmd) contains "CODE down" / "CODE up"
 * / "CODE tap", injects the event.
 */
#include <linux/uinput.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <signal.h>

static volatile sig_atomic_t g_run = 1;
static void on_sig(int s) { (void)s; g_run = 0; }

static void write_ev(int fd, int type, int code, int val)
{
	struct input_event e;
	memset(&e, 0, sizeof(e));
	e.type = (__u16)type; e.code = (__u16)code; e.value = (__s32)val;
	if (write(fd, &e, sizeof(e)) != (ssize_t)sizeof(e))
		fprintf(stderr, "inj: write failed: %s\n", strerror(errno));
}

int main(int argc, char **argv)
{
	const char *cmdpath = argc > 1 ? argv[1] : "/tmp/uinj_cmd";
	int fd = open("/dev/uinput", O_WRONLY | O_NONBLOCK);
	if (fd < 0) { perror("open uinput"); return 1; }
	ioctl(fd, UI_SET_EVBIT, EV_KEY);
	ioctl(fd, UI_SET_EVBIT, EV_SYN);
	for (int k = 1; k < 256; ++k)
		ioctl(fd, UI_SET_KEYBIT, k);
	struct uinput_setup us;
	memset(&us, 0, sizeof(us));
	us.id.bustype = BUS_USB;
	strcpy(us.name, "inj test keyboard");
	ioctl(fd, UI_DEV_SETUP, &us);
	if (ioctl(fd, UI_DEV_CREATE) != 0) { perror("UI_DEV_CREATE"); return 1; }
	signal(SIGTERM, on_sig);
	unlink(cmdpath);
	int last_code = -1, last_val = -1;
	while (g_run)
	{
		usleep(100000);
		FILE *f = fopen(cmdpath, "r");
		if (!f) continue;
		int code = -1; char act[8] = "";
		if (fscanf(f, "%d %s", &code, act) == 2)
		{
			if (!strcmp(act, "down")) { write_ev(fd, EV_KEY, code, 1); write_ev(fd, EV_SYN, SYN_REPORT, 0); last_code = code; last_val = 1; }
			else if (!strcmp(act, "up")) { write_ev(fd, EV_KEY, code, 0); write_ev(fd, EV_SYN, SYN_REPORT, 0); last_code = code; last_val = 0; }
			else if (!strcmp(act, "tap")) { write_ev(fd, EV_KEY, code, 1); write_ev(fd, EV_SYN, SYN_REPORT, 0); write_ev(fd, EV_KEY, code, 0); write_ev(fd, EV_SYN, SYN_REPORT, 0); }
		}
		fclose(f);
		unlink(cmdpath);
	}
	ioctl(fd, UI_DEV_DESTROY);
	close(fd);
	return 0;
}
