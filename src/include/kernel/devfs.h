#ifndef _DEVFS_H_INCLUDE
#define _DEVFS_H_INCLUDE

#include <kernel/vfs.h>

void devfs_init();
int devfs_newdevice(char* name, int type, dev_t dev, mode_t mode);
fscalls_t* devfs_getfuncs();

#endif
