#include <unistd.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>

#include "common.h"

int main() {
	char c = COMMAND_POWEROFF;
	int fd = open(COMMAND_FIFO, O_WRONLY);

	if (fd == -1) {
		perror("poweroff: open");
		return EXIT_FAILURE;
	}

	if (write(fd, &c, 1)) {
		perror("poweroff: write");
		return EXIT_FAILURE;
	}

	close(fd);
	return EXIT_SUCCESS;
}
