#include <sys/mount.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <mntent.h>
#include <errno.h>

struct mtab_list {
	struct mtab_list *next;
	char line[1000];
	struct mntent mntent;	
};

static const char *argv0;
static struct mtab_list *mtab_list;
static bool force = false;

void usage(void) {
	fprintf(stderr, "%s: usage: %s [-a] [-f] [-d device] [-t filesystem] [mountpoint]\n", argv0, argv0);
	exit(EXIT_FAILURE);
}

static int mount_filesystem(char *device, char *path, char *filesystem, unsigned long flags, void *opts) {
	if (device && strcmp(device, "none") == 0)
		device = NULL;

	// check if device already mounted
	if (!force) {
		for (struct mtab_list *it = mtab_list; it; it = it->next) {
			if (strcmp(path, it->mntent.mnt_dir) == 0 && strcmp(filesystem, it->mntent.mnt_type) == 0 && strcmp(it->mntent.mnt_fsname, "none") == 0) {
				fprintf(stderr, "%s: virtual filesystem %s already mounted at %s\n", argv0, filesystem, path);
				return EXIT_SUCCESS;
			}

			if (device && strcmp(device, it->mntent.mnt_fsname) == 0 && strcmp(it->mntent.mnt_fsname, "none")) {
				fprintf(stderr, "%s: %s already mounted at %s\n", argv0, device, path);
				return EXIT_SUCCESS;
			}
		}
	}

	#ifdef __astral__
	if (mount(device, path, filesystem, flags, opts)) {
		fprintf(stderr, "%s: failed to mount filesytem %s on device %s at %s: %s\n", argv0, filesystem, device, path, strerror(errno));
		return EXIT_FAILURE;
	}
	#else
	printf("mount command: mount(%s, %s, %s, 0, NULL)\n", device, path, filesystem);
	#endif

	struct mtab_list *entry = malloc(sizeof(struct mtab_list));
	if (entry == NULL)
		return EXIT_FAILURE;

	entry->mntent.mnt_fsname = device ? strdup(device) : "none";
	entry->mntent.mnt_dir = strdup(path);
	entry->mntent.mnt_type = strdup(filesystem);
	entry->mntent.mnt_opts = "rw";
	entry->mntent.mnt_freq = 0;
	entry->mntent.mnt_passno = 0;
	entry->next = mtab_list;
	mtab_list = entry;

	FILE *mtab = setmntent("/etc/mtab", "a");
	if (mtab) {
		addmntent(mtab, &entry->mntent);
		endmntent(mtab);
	}

	return 0;
}

static int mount_all(void) {
	FILE *fstab = setmntent("/etc/fstab", "r");
	struct mntent mntent;
	char buf[1000];

	if (fstab == NULL) {
		fprintf(stderr, "%s: setmntent /etc/fstab: %s\n", argv0, strerror(errno));
		endmntent(fstab);
		return EXIT_FAILURE;
	}

	for (;;) {
		errno = 0;
		struct mntent *p = getmntent_r(fstab, &mntent, buf, sizeof(buf));
		if (p == NULL) {
			if (errno) {
				fprintf(stderr, "%s: getmntent_r: %s\n", argv0, strerror(errno));
				endmntent(fstab);
				return EXIT_FAILURE;
			}

			break;
		}

		if (strcmp(mntent.mnt_type, "auto") == 0) {
			fprintf(stderr, "%s: no filesystem autodetection support\n", argv0);
			continue;
		}

		if (!(hasmntopt(&mntent, "defaults") || hasmntopt(&mntent, "auto")) || hasmntopt(&mntent, "noauto"))
			continue;

		mount_filesystem(mntent.mnt_fsname, mntent.mnt_dir, mntent.mnt_type, 0, NULL);
	}

	endmntent(fstab);
	return 0;
}

static int print_mounts(void) {
	FILE *mtab = setmntent("/etc/mtab", "r");
	struct mntent mntent;
	char buf[1000];
	
	if (mtab == NULL) {
		fprintf(stderr, "%s: setmntent /etc/mtab: %s\n", argv0, strerror(errno));
		return EXIT_FAILURE;
	}

	for (;;) {
		errno = 0;
		struct mntent *p = getmntent_r(mtab, &mntent, buf, sizeof(buf));
		if (p == NULL) {
			if (errno) {
				fprintf(stderr, "%s: getmntent_r: %s\n", argv0, strerror(errno));
				endmntent(mtab);
				return EXIT_FAILURE;
			}

			break;
		}

		printf("%s on %s type %s (rw)\n", mntent.mnt_fsname, mntent.mnt_dir, mntent.mnt_type);
	}

	endmntent(mtab);
	return 0;
}

static int get_fstab_info (const char *mount_point, char **fs, char **dev) {
	FILE *fstab = setmntent("/etc/fstab", "r");
	struct mntent mntent;
	char buf[1024];

	if (fstab == NULL) {
		fprintf(stderr, "%s: setmntent /etc/fstab: %s\n", argv0, strerror(errno));
		endmntent(fstab);
		return EXIT_FAILURE;
	}

	for (;;) {
		errno = 0;
		struct mntent *p = getmntent_r(fstab, &mntent, buf, sizeof(buf));
		if (p == NULL) {
			if (errno)
				fprintf(stderr, "%s: getmntent_r: %s\n", argv0, strerror(errno));
			else
				fprintf(stderr, "%s: no /etc/fstab entry for %s found.\n", argv0, mount_point);

			endmntent(fstab);
			return EXIT_FAILURE;
		}

		if (strcmp(mntent.mnt_dir, mount_point) == 0)
			break;
	}

	if (strcmp(mntent.mnt_fsname, "none")) {
		*dev = strdup(mntent.mnt_fsname);
		if (*dev == NULL) {
			fprintf(stderr, "%s: strdup: %s", argv0, strerror(errno));
			endmntent(fstab);
			return EXIT_FAILURE;
		}
	}

	if (strcmp(mntent.mnt_type, "auto") == 0) {
		fprintf(stderr, "%s: no filesystem autodetection support\n", argv0);
		endmntent(fstab);
		return EXIT_FAILURE;
	}

	*fs = strdup(mntent.mnt_type);
	if (*fs == NULL) {
		fprintf(stderr, "%s: strdup: %s", argv0, strerror(errno));
		endmntent(fstab);
		return EXIT_FAILURE;
	}

	endmntent(fstab);
	return EXIT_SUCCESS;
}

static void scan_mtab() {
	FILE *mtab = setmntent("/etc/mtab", "r");
	
	if (mtab == NULL) {
		fprintf(stderr, "%s: setmntent /etc/mtab: %s\n", argv0, strerror(errno));
		endmntent(mtab);
		return;
	}

	for (;;) {
		errno = 0;
		struct mtab_list *entry = malloc(sizeof(struct mtab_list));
		if (!entry)
			break;

		struct mntent *p = getmntent_r(mtab, &entry->mntent, entry->line, sizeof(entry->line));
		if (p == NULL) {
			if (errno) {
				fprintf(stderr, "%s: getmntent_r: %s\n", argv0, strerror(errno));
				endmntent(mtab);
			}

			break;
		}

		entry->next = mtab_list;
		mtab_list = entry;
	}

	endmntent(mtab);
}

int main(int argc, char *argv[]) {
	char *dev = NULL;
	char *mountp = NULL;
	char *fs = NULL;
	bool devnext = false;
	bool typenext = false;
	bool all = false;

	argv0 = argv[0];

	for (int i = 1; i < argc; ++i) {
		if (strcmp(argv[i], "-d") == 0) {
			devnext = true;
			continue;
		}

		if (strcmp(argv[i], "-t") == 0) {
			typenext = true;
			continue;
		}

		if (strcmp(argv[i], "-a") == 0) {
			all = true;
			continue;
		}

		if (strcmp(argv[i], "-f") == 0) {
			force = true;
			continue;
		}

		if (devnext) {
			devnext = false;
			dev = argv[i];
		} else if (typenext) {
			typenext = false;
			fs = argv[i];
		} else if (mountp == NULL)
			mountp = argv[i];
		else
			usage();
	}

	scan_mtab();

	if (all)
		return mount_all();

	if (mountp == NULL && fs == NULL && dev == NULL)
		return print_mounts();

	if (mountp == NULL)
		usage();

	mountp = realpath(mountp, NULL);
	if (mountp == NULL) {
		fprintf(stderr, "%s: realpath: %s\n", argv0, strerror(errno));
		return EXIT_FAILURE;
	}

	if (fs == NULL && dev == NULL) {
		if (get_fstab_info(mountp, &fs, &dev))
			return EXIT_FAILURE;
	} else if (fs == NULL) {
		fprintf(stderr, "%s: filesystem autodetection is not supported in the kernel. please provide a filesystem with -t\n", argv[0]);
		usage();
	}

	return mount_filesystem(dev, mountp, fs, 0, NULL);
}

