#ifndef _ABI_H
#define _ABI_H

#include <time.h>
#include <kernel/iovec.h>

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

#define NAME_MAX 255

typedef struct {
	ino_t d_ino;
	off_t d_off;
	unsigned short d_reclen;
	unsigned char d_type;
	char d_name[NAME_MAX+1];
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

#define RENAME_NOREPLACE 0x1

#define AF_LOCAL 1
#define AF_INET 2
#define AF_PACKET 17

#define SOCK_STREAM 1
#define SOCK_DGRAM 2
#define SOCK_RAW 3
#define SOCK_SEQPACKET 5

#define MSG_CMSG_CLOEXEC 0x40000000
#define MSG_CTRUNC 0x8
#define MSG_PEEK 2
#define MSG_DONTWAIT 0x40
#define MSG_WAITALL 0x100
#define MSG_NOSIGNAL 0x4000

#define TCP_NODELAY 1
#define TCP_KEEPIDLE 4
#define TCP_KEEPINTVL 5

#define IP_TOS 1

#define SO_DEBUG        1
#define SO_REUSEADDR    2
#define SO_TYPE         3
#define SO_ERROR        4
#define SO_DONTROUTE    5
#define SO_BROADCAST    6
#define SO_SNDBUF       7
#define SO_RCVBUF       8
#define SO_KEEPALIVE    9
#define SO_OOBINLINE    10
#define SO_NO_CHECK	11
#define SO_LINGER       13
#define SO_PEERCRED	17
#define SO_RCVLOWAT     18
#define SO_SNDLOWAT     19
#define SO_RCVTIMEO     20
#define SO_SNDTIMEO     21
#define SO_BINDTODEVICE 25
#define SO_ACCEPTCONN   30
#define SO_PROTOCOL     38
#define SO_DOMAIN       39

#define ARPHRD_ETHER    1
#define ARPHRD_LOOPBACK 772

#define SOL_SOCKET 1
#define SOL_TCP 6

typedef unsigned socklen_t;

#define ABISOCKADDR_UN_MAX 108

typedef struct {
	unsigned short type;
	char addr[14];
} abisockaddr_t;

typedef struct {
	uint16_t sin_family;
	uint16_t sin_port;
	uint32_t sin_addr;
} inaddr_t;

typedef struct {
	uint16_t sun_family;
	char sun_path[ABISOCKADDR_UN_MAX];
} unaddr_t;

typedef struct {
	uint16_t sll_family;
	uint16_t sll_protocol;
	int32_t sll_ifindex;
	uint16_t sll_hatype;
	uint8_t sll_pkttype;
	uint8_t sll_halen;
	uint8_t sll_addr[8];
} sockaddr_ll_t;

typedef struct {
	void *addr;
	socklen_t addrlen;
	iovec_t *iov;
	socklen_t iovcount;
	int padding0;
	void *msgctrl;
	socklen_t ctrllen;
	int padding1;
	int flags;
} msghdr_t;

#define IFNAMSIZ 16

#define SIOCGIFFLAGS 0x8913
#define SIOCSIFADDR	0x8916
#define SIOCGIFMTU 0x8921
#define SIOCGIFHWADDR 0x8927
#define SIOCGIFINDEX 0x8933
#define SIOCADDRT 0x890b
#define FIONREAD 0x541B
#define FIONBIO 0x5421

typedef struct {
	char name[IFNAMSIZ];
	union {
		abisockaddr_t addr;
		int mtu;
		int ifindex;
		short flags;
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

#define CLOCK_REALTIME 0
#define CLOCK_MONOTONIC 1
#define CLOCK_BOOTTIME 7

#endif
