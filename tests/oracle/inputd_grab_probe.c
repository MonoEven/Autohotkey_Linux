/* inputd_grab_probe.c -- check whether another process holds EVIOCGRAB on a
 * device.  Exit 0 when the device is grabbable (no exclusive grab held),
 * exit 1 when EVIOCGRAB returns EBUSY (someone is grabbing it), exit 2 on
 * other errors. */
#include <errno.h>
#include <fcntl.h>
#include <linux/input.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

int main(int argc, char **argv)
{
	if (argc != 2)
		return 2;
	int fd = open(argv[1], O_RDONLY | O_NONBLOCK);
	if (fd < 0)
	{
		perror("grab_probe open");
		return 2;
	}
	if (ioctl(fd, EVIOCGRAB, 1) == 0)
	{
		ioctl(fd, EVIOCGRAB, 0);
		printf("GRAB_AVAILABLE\n");
		close(fd);
		return 0;
	}
	printf("GRAB_HELD (%s)\n", strerror(errno));
	close(fd);
	return 1;
}
