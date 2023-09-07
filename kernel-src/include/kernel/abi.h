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

#define DT_UNKNOWN 0
#define DT_FIFO 1
#define DT_CHR 2
#define DT_DIR 4
#define DT_BLK 6
#define DT_REG 8
#define DT_LNK 10
#define DT_SOCK 12
#define DT_WHT 14

typedef struct {
	ino_t d_ino;
	off_t d_off;
	unsigned short d_reclen;
	unsigned char d_type;
	char d_name[1024];
} dent_t;

#define TYPE_FIFO 1
#define TYPE_CHARDEV 2
#define TYPE_DIR 4
#define TYPE_BLOCKDEV 6
#define TYPE_REGULAR 8
#define TYPE_LINK 10
#define TYPE_SOCKET 12

#define GETMODE(m) (0xFFF & (m))
#define GETTYPE(m) (((m) >> 12) & 0xF)
#define MAKETYPE(m) (((m) & 0xF) << 12)

#define TODEV(major,minor) (((major & 0xFFF) << 8) + (minor & 0xFF))
#define MAJORDEV(dev) ((dev >> 8) & 0xFFF)
#define MINORDEV(dev) (dev & 0xFF)

#define AT_SYMLINK_NOFOLLOW 0x100
#define AT_EMPTY_PATH 0x1000

#endif
