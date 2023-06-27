#ifndef FILE_H
#define FILE_H

#include <kernel/vfs.h>
#include <mutex.h>
#include <kernel/scheduler.h>

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
int fd_clone(proc_t *proc);

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

#define O_RDONLY 0
#define O_WRONLY 1
#define O_RDWR 2

#define AT_FDCWD -100
#define O_CREAT	0100
#define O_EXCL 0200
#define O_NOCTTY 0400
#define O_TRUNC	01000
#define O_APPEND 02000
#define O_NONBLOCK 04000
#define O_DSYNC 010000
#define O_ASYNC 020000
#define O_LARGEFILE 0100000
#define O_DIRECTORY 0200000
#define O_NOFOLLOW 0400000
#define O_NOATIME 01000000
#define O_CLOEXEC 02000000
#define O_SYNC 04010000
#define O_RSYNC 04010000
#define O_TMPFILE 020000000

#define FDTABLE_LIMIT 256

#endif
