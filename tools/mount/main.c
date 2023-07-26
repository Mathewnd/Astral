#include <sys/mount.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>

void usage(char *name) {
	fprintf(stderr, "%s: usage: %s [-d device] mountpoint filesystem\n", name, name);
	exit(EXIT_FAILURE);
}

int main(int argc, char *argv[]) {
	char *dev = NULL;
	char *mountp = NULL;
	char *fs = NULL;
	bool devnext = false;

	for (int i = 1; i < argc; ++i) {
		if (strcmp(argv[i], "-d") == 0) {
			devnext = true;
			continue;
		}

		if (devnext) {
			devnext = false;
			dev = argv[i];
		} else if (mountp == NULL)
			mountp = argv[i];
		else if (fs == NULL)
			fs = argv[i];
		else
			usage(argv[0]);
	}

	if (mount == NULL || fs == NULL)
		usage(argv[0]);

	if (mount(dev, mountp, fs, 0, NULL) == -1) {
		perror("mount failed");
		return EXIT_FAILURE;
	}

	return EXIT_SUCCESS;
}
