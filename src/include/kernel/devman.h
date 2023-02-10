#ifndef _DEVMAN_H_INCLUDE
#define _DEVMAN_H_INCLUDE

#include <sys/stat.h>
#include <poll.h>

#define MAJOR_CONSOLE 1
#define MAJOR_ZERO 2
#define MAJOR_NULL 3
#define MAJOR_FULL 4
#define MAJOR_FB 5
#define MAJOR_KB 6
#define MAJOR_E9OUT 7
#define MAJOR_MOUSE 8
#define MAJOR_BLOCK 9


typedef struct{
	int (*read)(int* error, int minor, void* buff, size_t count, size_t offset);
	int (*write)(int* error, int minor, void* buff, size_t count, size_t offset);
	int (*isatty)(int minor);
	int (*isseekable)(int minor, size_t* seekmax);
	int (*ioctl)(int minor, unsigned long request, void* arg, int* result);
	int (*poll)(int minor, pollfd* fd);
	int (*map)(int minor, void* addr, size_t len, size_t offset, size_t mmuflags);
} devcalls;

int devman_read(int *error, int dev, void* buff, size_t count, size_t offset);
int devman_write(int *error, int dev, void* buff, size_t count, size_t offset);
int devman_newdevice(char* name, int type, int major, int minor, devcalls* calls);
int devman_isatty(int dev);
int devman_isseekable(int dev, size_t* seekmax);
int devman_ioctl(int dev, unsigned long request, void* arg, int* result);
int devman_poll(int dev, pollfd* fd);
int devman_map(int dev, void* addr, size_t len, size_t offset, size_t mmuflags);
void devman_init();

#endif
