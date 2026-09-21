#include <errno.h>
#include <fcntl.h>
#include <linux/hidraw.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

static int read_ram(int fd, unsigned addr, unsigned char *data, int len)
{
	unsigned char report[9] = { 0, 0xb5, addr >> 8, addr };

	if (ioctl(fd, HIDIOCSFEATURE(sizeof(report)), report) < 0)
		return -1;
	memset(report, 0, sizeof(report));
	if (ioctl(fd, HIDIOCGFEATURE(sizeof(report)), report) < 0)
		return -1;
	if (report[1] != 0xb5 || report[2] != (unsigned char)(addr >> 8) ||
	    report[3] != (unsigned char)addr) {
		errno = EPROTO;
		return -1;
	}
	memcpy(data, report + 4, len);
	return 0;
}

static int open_ms2130(const char *path)
{
	struct hidraw_devinfo info;
	char name[32];
	int fd, i;

	if (path)
		return open(path, O_RDWR);
	for (i = 0; i < 32; i++) {
		snprintf(name, sizeof(name), "/dev/hidraw%d", i);
		fd = open(name, O_RDWR);
		if (fd >= 0 && ioctl(fd, HIDIOCGRAWINFO, &info) == 0 &&
		    info.vendor == 0x534d && info.product == 0x2130)
			return fd;
		if (fd >= 0)
			close(fd);
	}
	errno = ENODEV;
	return -1;
}

int main(int argc, char **argv)
{
	unsigned char size[4], no_signal;
	unsigned width = 0, height = 0, lock = 0, i;
	int fd = open_ms2130(argc > 1 ? argv[1] : NULL);

	if (fd < 0)
		goto error;
	for (i = 0; i < 20; i++) {
		if (read_ram(fd, 0xf660, size, 4) ||
		    read_ram(fd, 0xf6e9, &no_signal, 1))
			goto error;
		width = size[0] | size[1] << 8;
		height = size[2] | size[3] << 8;
		if (!no_signal)
			lock++;
		usleep(50000);
	}
	close(fd);
	printf("signal=%s width=%u height=%u lock=%u/20\n",
	       lock ? "yes" : "no", width, height, lock);
	return !lock || !width || !height;

error:
	fprintf(stderr, "MS2130 status: %s\n", strerror(errno));
	if (fd >= 0)
		close(fd);
	return 2;
}
