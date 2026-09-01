/* inputd_output_watch.c -- independent target observer for ahk-inputd's
 * broker-owned uinput device.  It never speaks the broker protocol: it opens
 * the kernel event node named "ahk-inputd virtual keyboard" and prints EV_KEY
 * frames, proving the target-facing sequence after arbitration.
 *
 * Usage: inputd_output_watch [--count N] [--timeout-ms N]
 */
#define _GNU_SOURCE
#include <fcntl.h>
#include <linux/input.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int find_node(void)
{
	for (int attempt = 0; attempt < 100; ++attempt)
	{
		for (int i = 0; i < 128; ++i)
		{
			char sys[128];
			snprintf(sys, sizeof(sys), "/sys/class/input/event%d/device/name", i);
			FILE *f = fopen(sys, "r");
			if (!f) continue;
			char name[256] = {0};
			if (fgets(name, sizeof(name), f)
				&& strstr(name, "ahk-inputd virtual keyboard"))
			{
				fclose(f);
				return i;
			}
			fclose(f);
		}
		usleep(50000);
	}
	return -1;
}

int main(int argc, char **argv)
{
	int count = 2;
	int timeout_ms = 5000;
	for (int i = 1; i < argc; ++i)
	{
		if (!strcmp(argv[i], "--count") && i + 1 < argc) count = atoi(argv[++i]);
		else if (!strcmp(argv[i], "--timeout-ms") && i + 1 < argc) timeout_ms = atoi(argv[++i]);
		else return 2;
	}
	int node = find_node();
	if (node < 0) { fprintf(stderr, "output_watch: uinput node not found\n"); return 1; }
	char path[64];
	snprintf(path, sizeof(path), "/dev/input/event%d", node);
	int fd = open(path, O_RDONLY | O_NONBLOCK);
	if (fd < 0) { perror(path); return 1; }
	printf("OUTPUT_DEVICE %s\n", path); fflush(stdout);
	struct pollfd pfd = {fd, POLLIN, 0};
	int seen = 0;
	while (timeout_ms > 0 && seen < count)
	{
		int step = timeout_ms > 200 ? 200 : timeout_ms;
		int pr = poll(&pfd, 1, step);
		timeout_ms -= step;
		if (pr <= 0) continue;
		struct input_event ev;
		while (read(fd, &ev, sizeof(ev)) == (ssize_t)sizeof(ev))
			if (ev.type == EV_KEY)
			{
				printf("OUT code=%u value=%d\n", (unsigned)ev.code, ev.value);
				fflush(stdout);
				if (++seen >= count) break;
			}
	}
	close(fd);
	printf("OUTPUT_END count=%d\n", seen);
	return seen >= count ? 0 : 1;
}
