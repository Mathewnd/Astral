#ifndef _STAT_H_INCLUDE
#define _STAT_H_INCLUDE

#include <sys/types.h>

#define TYPE_FIFO 1
#define TYPE_CHARDEV 2
#define TYPE_DIR 4
#define TYPE_BLOCKDEV 6
#define TYPE_REGULAR 8
#define TYPE_IFLNK 10
#define TYPE_IFSOCK 12

#define GETMODE(m) (0xFFF & m)
#define GETTYPE(m) ((m >> 12) & 0xF)
#define MAKETYPE(m) ((m & 0xF) << 12)

typedef struct {
	dev_t	st_dev;
	ino_t	st_ino;
	mode_t	st_mode;
	nlink_t	st_nlink;
	uid_t	st_uid;
	gid_t	st_gid;
	dev_t	st_rdev;
	off_t	st_size;

	blksize_t st_blksize;
	blkcnt_t  st_blocks;
} stat;

#endif
