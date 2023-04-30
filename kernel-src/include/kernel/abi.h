#ifndef _ABI_H
#define _ABI_H

typedef int pid_t;
typedef int tid_t;
typedef int gid_t;
typedef int uid_t;
typedef unsigned int mode_t;
typedef uint64_t ino_t;

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

#endif
