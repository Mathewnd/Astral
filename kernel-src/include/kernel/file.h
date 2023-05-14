#ifndef FILE_H
#define FILE_H

#include <kernel/vfs.h>
#include <mutex.h>

typedef struct file_t {
	vnode_t *vnode;
	mutex_t mutex;
	int refcount;
	mode_t mode;
	uintmax_t offset;
	int flags;
} file_t;

typedef struct fd_t {
	file_t *file;
	int flags;
} fd_t;

file_t *fd_allocate();
file_t *fd_get(int fd);
void fd_release(file_t *file);
int fd_new(int flags, file_t **file, int *fd);
int fd_close(int fd);
int fd_dup(int oldfd, int newfd, bool exact, int flags, int *ret);

#define FILE_READ 1
#define FILE_WRITE 2
#define FILE_KNOWN_FLAGS 3

static inline int fileflagstovnodeflags(int flags) {
	int vnflags = 0;
	if (flags & FILE_READ)
		vnflags |= V_FFLAGS_READ;
	if (flags & FILE_WRITE)
		vnflags |= V_FFLAGS_WRITE;

	return vnflags;
}

#define AT_FDCWD -100

#endif
