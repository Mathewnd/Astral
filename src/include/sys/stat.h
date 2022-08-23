#ifndef _STAT_H_INCLUDE
#define _STAT_H_INCLUDE

#include <sys/types.h>

#define TYPE_FIFO 1
#define TYPE_CHARDEV 2
#define TYPE_DIR 4
#define TYPE_BLOCKDEV 6
#define TYPE_REGULAR 8
#define TYPE_LINK 10
#define TYPE_SOCKET 12

#define GETMODE(m) (0xFFF & m)
#define GETTYPE(m) ((m >> 12) & 0xF)
#define MAKETYPE(m) ((m & 0xF) << 12)

typedef long time_t;

struct timespec {
        time_t tv_sec;
        long tv_nsec;
};


typedef struct {
        dev_t st_dev;
        ino_t st_ino;
        nlink_t st_nlink;
        mode_t st_mode;
        uid_t st_uid;
        gid_t st_gid;
        unsigned int __pad0;
        dev_t st_rdev;
        off_t st_size;
        blksize_t st_blksize;
        blkcnt_t st_blocks;
        struct timespec st_atim;
        struct timespec st_mtim;
        struct timespec st_ctim;
        long __unused[3];
} stat;

#endif
