#include <unistd.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>

#include "common.h"

int main() {
	if (daemon(0, 0)) {
		perror("acpid: daemon");
		return EXIT_FAILURE;
	}

	int fd = open("/dev/acpi", O_RDONLY);
	if (fd == -1) {
		perror("acpid: open");
		return EXIT_FAILURE;
	}

	for (;;) {
		char c;
		int n = read(fd, &c, 1);

		int cmdfd = open(COMMAND_FIFO, O_WRONLY);
		if (cmdfd == -1) {
			perror("acpid: open");
			continue;
		}

		switch (c) {
			case 'p':
				if (write(cmdfd, &c, 1) < 0)
					perror("acpid: write");
				break;
			default:
				fprintf(stderr, "acpid: unknown acpi event %c\n", c);
		}

		close(cmdfd);
	}

	close(fd);
	return EXIT_SUCCESS;
}
