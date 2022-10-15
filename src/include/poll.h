#ifndef _POLL_H_INCLUDE
#define _POLL_H_INCLUDE

typedef struct{
	int   fd;
	short events;
	short revents;
} pollfd;

#define POLLIN 0x0001
#define POLLPRI 0x0002
#define POLLOUT 0x0004
#define POLLERR 0x0008
#define POLLHUP 0x0010
#define POLLNVAL 0x0020
#define POLLRDNORM 0x0040
#define POLLRDBAND 0x0080
#define POLLWRNORM 0x0100
#define POLLRDHUP 0x2000

#endif
