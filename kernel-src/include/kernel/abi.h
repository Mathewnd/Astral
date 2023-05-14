#ifndef _ABI_H
#define _ABI_H

#include <time.h>

typedef int pid_t;
typedef int tid_t;
typedef int gid_t;
typedef int uid_t;
typedef unsigned int mode_t;
typedef uint64_t ino_t;
typedef long off_t;
typedef uint64_t dev_t;
typedef unsigned long nlink_t;
typedef long blksize_t;
typedef uint64_t blkcnt_t;

#define POLLIN 0x1
#define POLLPRI 0x2
#define POLLOUT 0x4
#define POLLERR 0x8
#define POLLHUP 0x10
#define POLLNVAL 0x20
#define POLLRDNORM 0x40
#define POLLRDBAND 0x80
#define POLLWRNORM 0x100
#define POLLRDHUP 0x2000

typedef struct {
	dev_t dev;
	ino_t ino;
	nlink_t nlink;
	mode_t mode;
	uid_t uid;
	gid_t gid;
	unsigned int __pad0;
	dev_t rdev;
	off_t size;
	blksize_t blksize;
	blkcnt_t blocks;
	timespec_t atim;
	timespec_t mtim;
	timespec_t ctim;
	long __unused[3];
} stat_t;

#endif
