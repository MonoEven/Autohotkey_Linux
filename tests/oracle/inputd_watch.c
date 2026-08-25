/* inputd_watch.c -- observe the ahk-inputd replay device (vendor 0x0FAC).
 *
 * Prints one line per EV_KEY: "KEY code=N value=V" for the virtual keyboard
 * created by ahk-inputd, so an oracle can prove which events the broker
 * replayed (and, by absence, which it suppressed).
 */
#include <linux/input.h>
#include <sys/ioctl.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <limits.h>

#define REPLAY_VENDOR 0x0FAC

static int is_keyboard_fd(int fd)
{
	unsigned char bits[KEY_CNT / 8];
	memset(bits, 0, sizeof(bits));
	if (ioctl(fd, EVIOCGBIT(EV_KEY, sizeof(bits)), bits) < 0) return 0;
	if (!(bits[KEY_A / 8] & (1 << (KEY_A & 7)))) return 0;
	if (!(bits[KEY_ENTER / 8] & (1 << (KEY_ENTER & 7)))) return 0;
	return 1;
}

/* probe: verify a keyboard EVIOCGRAB succeeds (proves a dead broker released
 * its grabs).  Prints OK only when at least one real keyboard can be grabbed. */
static int probe_grab(void)
{
	DIR *dir = opendir("/dev/input");
	if (!dir) return 1;
	struct dirent *ent;
	int ok = 0;
	while ((ent = readdir(dir)) != NULL)
	{
		if (strncmp(ent->d_name, "event", 5) != 0) continue;
		char path[PATH_MAX];
		if (snprintf(path, sizeof(path), "/dev/input/%s", ent->d_name) >= (int)sizeof(path))
			continue;
		int fd = open(path, O_RDONLY | O_NONBLOCK);
		if (fd < 0) continue;
		if (is_keyboard_fd(fd) && ioctl(fd, EVIOCGRAB, 1) == 0)
		{
			ioctl(fd, EVIOCGRAB, 0);
			ok = 1;
		}
		close(fd);
		if (ok) break;
	}
	closedir(dir);
	printf("%s\n", ok ? "GRAB_OK" : "GRAB_BLOCKED");
	return ok ? 0 : 1;
}

static int open_replay(void)
{
	DIR *dir = opendir("/dev/input");
	if (!dir) return -1;
	struct dirent *ent;
	while ((ent = readdir(dir)) != NULL)
	{
		if (strncmp(ent->d_name, "event", 5) != 0) continue;
		char path[PATH_MAX];
		if (snprintf(path, sizeof(path), "/dev/input/%s", ent->d_name) >= (int)sizeof(path))
			continue;
		int fd = open(path, O_RDONLY | O_NONBLOCK);
		if (fd < 0) continue;
		struct input_id id;
		if (ioctl(fd, EVIOCGID, &id) == 0 && id.vendor == REPLAY_VENDOR)
		{
			closedir(dir);
			return fd;
		}
		close(fd);
	}
	closedir(dir);
	return -1;
}

int main(int argc, char **argv)
{
	if (argc > 1 && !strcmp(argv[1], "probe"))
		return probe_grab();
	(void)argc; (void)argv;
	int fd = open_replay();
	if (fd < 0)
	{
		fprintf(stderr, "inputd_watch: no replay device\n");
		return 1;
	}
	struct input_event ev;
	for (;;)
	{
		ssize_t r = read(fd, &ev, sizeof(ev));
		if (r != (ssize_t)sizeof(ev))
		{
			if (r < 0 && (errno == EINTR || errno == EAGAIN)) { usleep(10000); continue; }
			if (r == 0) { usleep(10000); continue; }
			break;
		}
		if (ev.type == EV_KEY)
			printf("KEY code=%u value=%d\n", (unsigned)ev.code, (int)ev.value), fflush(stdout);
	}
	return 0;
}
