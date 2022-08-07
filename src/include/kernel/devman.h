#ifndef _DEVMAN_H_INCLUDE
#define _DEVMAN_H_INCLUDE

#include <sys/stat.h>


typedef struct{
	int (*read)(int* error, int minor, void* buff, size_t count, size_t offset);
	int (*write)(int* error, int minor, void* buff, size_t count, size_t offset);
} devcalls;

int devman_read(int *error, int dev, void* buff, size_t count, size_t offset);
int devman_write(int *error, int dev, void* buff, size_t count, size_t offset);
int devman_newdevice(char* name, int type, int major, int minor, devcalls* calls);
void devman_init();

#endif
