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



typedef struct {
        vnode_t* node;
        off_t offset;
        int flags;
	int lock;
} fd_t;


#endif
