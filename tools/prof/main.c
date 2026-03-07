#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/poll.h>
#include <errno.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <sys/ioctl.h>
#include <termios.h>

static char *progname;

static int open_or_die(const char *name, int flag, mode_t mode) {
	int fd = open(name, flag, mode);
	if (fd < 0) {
		fprintf(stderr, "%s: open %s: %s\n", progname, name, strerror(errno));
		exit(EXIT_FAILURE);
	}
	return fd;
}

int main(int argc, char *argv[]) {
	progname = argv[0];
	if (argc != 2) {
		fprintf(stderr, "%s: usage: %s FILE\n", argv[0], argv[0]);
		return EXIT_FAILURE;
	}

	// right now the limit for each trace is 256
	uintptr_t buf[256];

	int infd = open_or_die("/dev/prof", O_RDONLY | O_NONBLOCK, 0);
	int outfd = open_or_die(argv[1], O_WRONLY | O_CREAT, 0644);

	// enable raw mode
	if (isatty(outfd)) {
		struct termios termios;
		tcgetattr(outfd, &termios);
		cfmakeraw(&termios);
		termios.c_oflag &= ~ONLCR;
		tcsetattr(outfd, TCSANOW, &termios);
	}

	printf("%s: draining /dev/prof\n", argv[0]);
	// empty it
	if (ioctl(infd, 12345678, NULL) < 0)
		return EXIT_FAILURE;

	struct pollfd pollfd[2];

	pollfd[0].fd = 0;
	pollfd[0].events = POLLIN;
	pollfd[0].revents = 0;
	pollfd[1].fd = infd;
	pollfd[1].events = POLLIN;
	pollfd[1].revents = 0;

	printf("%s: working...\n", argv[0]);
	while (poll(pollfd, 2, -1)) {
		if (pollfd[0].revents)
			break;

		uint8_t count;
		read(infd, &count, 1);
		read(infd, buf, 8 * count);
		write(outfd, &count, 1);
		write(outfd, buf, 8 * count);
	}

	printf("%s: done\n", argv[0]);

	return EXIT_SUCCESS;
}
