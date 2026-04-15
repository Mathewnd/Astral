#define _GNU_SOURCE
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <stdlib.h>

int main(int argc, char *argv[]) {
	char hostname[1000];

	if (argc == 1) {
		if (gethostname(hostname, 1000)) {
			fprintf(stderr, "%s: failed to get hostname: %s\n", argv[0], strerror(errno));
			return EXIT_FAILURE;
		}

		printf("%s\n", hostname);
	} else if (argc == 2) {
		if (sethostname(argv[1], strlen(argv[1]))) {
			fprintf(stderr, "%s: failed to set hostname: %s\n", argv[0], strerror(errno));
			return EXIT_FAILURE;
		}
	} else {
		fprintf(stderr, "%s: usage: %s [hostname]\n", argv[0], argv[0]);
		return EXIT_FAILURE;
	}

	return EXIT_SUCCESS;
}
