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

#define AF_LOCAL 1
#define AF_INET 2

#define SOCK_STREAM 1
#define SOCK_DGRAM 2
#define SOCK_RAW 3

#define MSG_CTRUNC 0x8
#define MSG_PEEK 2
#define MSG_WAITALL 0x100
#define MSG_NOSIGNAL 0x4000

#define SO_BROADCAST 6
#define SO_BINDTODEVICE 25
#define SO_KEEPALIVE 9

#define SOL_SOCKET 1

typedef struct {
	unsigned short type;
	char addr[14];
} abisockaddr_t;

typedef struct {
	uint16_t sin_family;
	uint16_t sin_port;
	uint32_t sin_addr;
} __attribute__((packed)) inaddr_t;

typedef struct {
	uint16_t sun_family;
	char sun_path[108];
} __attribute__((packed)) unaddr_t;

typedef struct {
	void *addr;
	size_t len;
} iovec_t;

static inline size_t iovec_size(iovec_t *iovec, size_t count) {
	size_t size = 0;

	for (int i = 0; i < count; ++i)
		size += iovec[i].len;

	return size;
}

typedef struct {
	void *addr;
	size_t addrlen;
	iovec_t *iov;
	size_t iovcount;
	void *msgctrl;
	size_t ctrllen;
	int flags;
} msghdr_t;

#define IFNAMSIZ 16
#define SIOCGIFHWADDR 0x8927
#define SIOCADDRT 0x890b
#define SIOCSIFADDR	0x8916
#define FIONREAD 0x541B

typedef struct {
	char name[IFNAMSIZ];
	union {
		abisockaddr_t addr;
	};
} ifreq_t;

typedef struct {
	unsigned long int rt_pad1;
	abisockaddr_t rt_dst;
	abisockaddr_t rt_gateway;
	abisockaddr_t rt_genmask;
	unsigned short int rt_flags;
	short int rt_pad2;
	unsigned long int rt_pad3;
	unsigned char rt_tos;
	unsigned char rt_class;
	short int rt_pad4[3];
	short int rt_metric;
	char *rt_dev;
	unsigned long int rt_mtu;
	unsigned long int rt_window;
	unsigned short int rt_irtt;
} abirtentry_t;

#define HOST_NAME_MAX 64

#endif
