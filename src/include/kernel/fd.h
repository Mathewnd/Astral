#ifndef _FD_H_INCLUDE
#define _FD_H_INCLUDE

#include <kernel/vfs.h>
#include <sys/types.h>

#define FD_FLAGS_READ 1
#define FD_FLAGS_WRITE 2

#define O_RDONLY 00
#define O_WRONLY 01
#define O_RDWR 02
#define O_ACCMODE 001000003

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

#define MAX_FD 512
#define AT_FDCWD -100

#include <stdint.h>
#include <stddef.h>

typedef struct {
	uintmax_t refcount;
        vnode_t* node;
        off_t offset;
        int flags;
	int lock;
	mode_t mode;
} fd_t;

typedef struct {
	int lock;
	size_t fdcount;
	fd_t** fd;
} fdtable_t; 

int fd_alloc(fdtable_t* fdtable, fd_t** fd, int* ifd, int lowest);
int fd_free(fdtable_t* fdtable, int ifd);
int fd_access(fdtable_t* fdtable, fd_t** fd, int ifd);
int fd_release(fd_t* fd);
int fd_tableinit(fdtable_t* fdtable);
int fd_tableclone(fdtable_t* source, fdtable_t* dest);
int fd_duplicate(fdtable_t* table, int src, int dest, int type, int* ret);

#endif
