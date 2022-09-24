#ifndef _DEVMAN_H_INCLUDE
#define _DEVMAN_H_INCLUDE

#include <sys/stat.h>

#define MAJOR_CONSOLE 1
#define MAJOR_ZERO 2
#define MAJOR_NULL 3
#define MAJOR_FULL 4
#define MAJOR_FB 5

typedef struct{
	int (*read)(int* error, int minor, void* buff, size_t count, size_t offset);
	int (*write)(int* error, int minor, void* buff, size_t count, size_t offset);
	int (*isatty)(int minor);
	int (*isseekable)(int minor, size_t* seekmax);
} devcalls;

int devman_read(int *error, int dev, void* buff, size_t count, size_t offset);
int devman_write(int *error, int dev, void* buff, size_t count, size_t offset);
int devman_newdevice(char* name, int type, int major, int minor, devcalls* calls);
int devman_isatty(int dev);
int devman_isseekable(int dev, size_t* seekmax);
void devman_init();

#endif
