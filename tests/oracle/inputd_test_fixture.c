/* inputd_test_fixture.c -- uinput keyboard fixture for inputd fault oracles.
 *
 * Creates a synthetic keyboard (EV_KEY: letters + Enter, so inputd's
 * is_keyboard() accepts it), prints its /dev/input/eventX node to
 * /tmp/inputd_fixture_dev, then drives keys from the command line:
 *
 *   usage: inputd_test_fixture --hold <code> [<code>...]   press and hold
 *          inputd_test_fixture --tap  <code> <code> ...     press+release once
 *          inputd_test_fixture --seq-trigger COUNT CODE PATH
 *          inputd_test_fixture --split-trigger CODE DOWN_PATH UP_PATH
 *          inputd_test_fixture --script-trigger PATH CODE:VALUE...
 *          inputd_test_fixture --two-trigger CODE1 PATH1 [CODE2] PATH2
 *
 * Non-tap modes keep the uinput device alive until killed.
 */
#define _GNU_SOURCE
#include <fcntl.h>
#include <linux/input.h>
#include <linux/uinput.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

static void emit(int fd, int type, int code, int value)
{
	struct input_event ev;
	memset(&ev, 0, sizeof(ev));
	gettimeofday(&ev.time, NULL);
	ev.type = (__u16)type;
	ev.code = (__u16)code;
	ev.value = value;
	if (write(fd, &ev, sizeof(ev)) != (ssize_t)sizeof(ev))
	{
		perror("fixture write");
		exit(2);
	}
}

static int create_device(void)
{
	int fd = open("/dev/uinput", O_WRONLY | O_NONBLOCK);
	if (fd < 0)
	{
		perror("/dev/uinput");
		return -1;
	}
	if (ioctl(fd, UI_SET_EVBIT, EV_KEY) != 0
		|| ioctl(fd, UI_SET_EVBIT, EV_SYN) != 0)
		goto fail;
	for (int c = 1; c < 128; ++c)
		if (ioctl(fd, UI_SET_KEYBIT, c) != 0)
			goto fail;
	struct uinput_setup setup;
	memset(&setup, 0, sizeof(setup));
	setup.id.bustype = BUS_USB;
	setup.id.vendor = 0xFACE;
	setup.id.product = 0x0001;
	setup.id.version = 1;
	const char *name_suffix = getenv("AHK_FIXTURE_NAME");
	snprintf(setup.name, UINPUT_MAX_NAME_SIZE, "inputd-test keyboard %s"
		, name_suffix && *name_suffix ? name_suffix : "");
	if (ioctl(fd, UI_DEV_SETUP, &setup) != 0
		|| ioctl(fd, UI_DEV_CREATE) != 0)
		goto fail;
	return fd;
fail:
	perror("fixture uinput setup");
	close(fd);
	return -1;
}

static int find_node(void)
{
	/* The device name carries the AHK_FIXTURE_NAME suffix; scan sysfs for it. */
	const char *name_suffix = getenv("AHK_FIXTURE_NAME");
	char want[UINPUT_MAX_NAME_SIZE];
	snprintf(want, sizeof(want), "inputd-test keyboard %s"
		, name_suffix && *name_suffix ? name_suffix : "");
	for (int i = 0; i < 64; ++i)
	{
		char path[128];
		snprintf(path, sizeof(path), "/sys/class/input/event%d/device/name", i);
		FILE *f = fopen(path, "r");
		if (!f)
			continue;
		char name[UINPUT_MAX_NAME_SIZE] = { 0 };
		if (fgets(name, sizeof(name), f))
			if (strstr(name, want))
			{
				fclose(f);
				return i;
			}
		fclose(f);
	}
	return -1;
}

int main(int argc, char **argv)
{
	if (argc < 2)
	{
		fprintf(stderr, "usage: %s --hold|--tap|--seq COUNT|--release-after MS|--idle CODE...\n", argv[0]);
		return 2;
	}
	int hold = strcmp(argv[1], "--hold") == 0;
	int tap = strcmp(argv[1], "--tap") == 0;
	int seq = strcmp(argv[1], "--seq") == 0;
	int seq_delayed = strcmp(argv[1], "--seq-delayed") == 0;
	int seq_trigger = strcmp(argv[1], "--seq-trigger") == 0;
	int split_trigger = strcmp(argv[1], "--split-trigger") == 0;
	int script_trigger = strcmp(argv[1], "--script-trigger") == 0;
	int two_trigger = strcmp(argv[1], "--two-trigger") == 0;
	int release_after = strcmp(argv[1], "--release-after") == 0;
	int idle = strcmp(argv[1], "--idle") == 0;
	if (!hold && !tap && !seq && !seq_delayed && !seq_trigger && !split_trigger
		&& !script_trigger && !two_trigger && !release_after && !idle)
		return 2;
	int seq_count = 0;
	if (seq || seq_delayed || seq_trigger)
	{
		seq_count = seq_delayed ? atoi(argv[3]) : atoi(argv[2]);
		if (seq_count < 1)
			return 2;
	}
	int seq_delay_ms = 0;
	if (seq_delayed)
	{
		seq_delay_ms = atoi(argv[2]);
		if (seq_delay_ms < 0)
			return 2;
	}
	const char *seq_trigger_path = NULL;
	if (seq_trigger)
		seq_trigger_path = argv[4];
	const char *split_down_path = NULL, *split_up_path = NULL;
	const char *script_trigger_path = NULL;
	const char *two_first_path = NULL, *two_second_path = NULL;
	int two_first_code = 0, two_second_code = 0;
	int split_code = 0;
	if (script_trigger)
	{
		if (argc < 4) return 2;
		script_trigger_path = argv[2];
	}
	if (two_trigger)
	{
		if (argc == 5)
		{
			two_first_code = two_second_code = atoi(argv[2]);
			two_first_path = argv[3];
			two_second_path = argv[4];
		}
		else if (argc >= 6)
		{
			two_first_code = atoi(argv[2]);
			two_first_path = argv[3];
			two_second_code = atoi(argv[4]);
			two_second_path = argv[5];
		}
		else return 2;
	}
	if (split_trigger)
	{
		if (argc < 5) return 2;
		split_code = atoi(argv[2]);
		split_down_path = argv[3];
		split_up_path = argv[4];
	}
	int release_ms = 0;
	if (release_after)
	{
		release_ms = atoi(argv[2]);
		if (release_ms < 100)
			return 2;
	}
	int fd = create_device();
	if (fd < 0)
		return 1;
	usleep(150000); /* let udev enumerate */
	int node = find_node();
	if (node < 0)
	{
		fprintf(stderr, "fixture: device node not enumerated\n");
		return 1;
	}
	const char *devpath = getenv("AHK_FIXTURE_DEVPATH");
	if (!devpath || !*devpath)
		devpath = "/tmp/inputd_fixture_dev";
	FILE *path = fopen(devpath, "w");
	if (path)
	{
		fprintf(path, "/dev/input/event%d\n", node);
		fclose(path);
	}
	fprintf(stderr, "fixture: /dev/input/event%d\n", node);
	int start = (seq || release_after) ? 3 : 2;
	if (seq || seq_delayed || seq_trigger)
	{
		/* --seq COUNT CODE / --seq-delayed MS COUNT CODE /
		 * --seq-trigger COUNT CODE PATH: tap CODE COUNT times (200ms apart),
		 * then keep the device alive.  seq-delayed waits MS before the first
		 * tap; seq-trigger waits until PATH exists (test-side determinism). */
		int code = seq ? atoi(argv[3]) : atoi(argv[4]);
		if (seq_trigger)
			code = atoi(argv[3]);
		if (seq_delay_ms)
			usleep((useconds_t)seq_delay_ms * 1000);
		if (seq_trigger_path)
		{
			for (;;)
			{
				if (access(seq_trigger_path, F_OK) == 0)
					break;
				usleep(20000);
			}
		}
		for (int i = 0; i < seq_count; ++i)
		{
			emit(fd, EV_KEY, code, 1);
			emit(fd, EV_SYN, SYN_REPORT, 0);
			usleep(50000);
			emit(fd, EV_KEY, code, 0);
			emit(fd, EV_SYN, SYN_REPORT, 0);
			usleep(150000);
		}
		for (;;)
			pause();
	}
	if (two_trigger)
	{
		/* --two-trigger CODE FIRST_PATH SECOND_PATH: two independently
		 * released taps, used by dynamic HotIf false/true tests. */
		const char *paths[2] = {two_first_path, two_second_path};
		int codes[2] = {two_first_code, two_second_code};
		for (int i = 0; i < 2; ++i)
		{
			while (access(paths[i], F_OK) != 0) usleep(20000);
			emit(fd, EV_KEY, codes[i], 1); emit(fd, EV_SYN, SYN_REPORT, 0);
			usleep(70000);
			emit(fd, EV_KEY, codes[i], 0); emit(fd, EV_SYN, SYN_REPORT, 0);
		}
		for (;;) pause();
	}
	if (script_trigger)
	{
		/* --script-trigger PATH CODE:VALUE...: deterministic multi-key
		 * sequence for modifier/wildcard/key-up pipeline tests. */
		while (access(script_trigger_path, F_OK) != 0) usleep(20000);
		for (int i = 3; i < argc; ++i)
		{
			char *colon = strchr(argv[i], ':');
			if (!colon) return 2;
			int code = atoi(argv[i]);
			int value = atoi(colon + 1);
			emit(fd, EV_KEY, code, value);
			emit(fd, EV_SYN, SYN_REPORT, 0);
			usleep(70000);
		}
		for (;;) pause();
	}
	if (split_trigger)
	{
		/* --split-trigger CODE DOWN_PATH UP_PATH: deterministic down/up
		 * separation for arbitration key-up ownership/crash tests. */
		while (access(split_down_path, F_OK) != 0) usleep(20000);
		emit(fd, EV_KEY, split_code, 1);
		emit(fd, EV_SYN, SYN_REPORT, 0);
		while (access(split_up_path, F_OK) != 0) usleep(20000);
		emit(fd, EV_KEY, split_code, 0);
		emit(fd, EV_SYN, SYN_REPORT, 0);
		for (;;) pause();
	}
	if (idle)
	{
		/* --idle: no events, device stays alive. */
		for (;;)
			pause();
	}
	for (int i = start; i < argc; ++i)
	{
		int code = atoi(argv[i]);
		emit(fd, EV_KEY, code, 1);
		emit(fd, EV_SYN, SYN_REPORT, 0);
		if (tap)
		{
			usleep(60000);
			emit(fd, EV_KEY, code, 0);
			emit(fd, EV_SYN, SYN_REPORT, 0);
			usleep(60000);
		}
		else if (release_after)
		{
			usleep((useconds_t)release_ms * 1000);
			emit(fd, EV_KEY, code, 0);
			emit(fd, EV_SYN, SYN_REPORT, 0);
		}
	}
	if (tap)
		_exit(0); /* fixture tap: done; device disappears with the process. */
	/* hold / seq / release-after: keep the device alive until killed. */
	for (;;)
		pause();
	return 0;
}
