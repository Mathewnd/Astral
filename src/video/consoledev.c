#include <kernel/consoledev.h>
#include <kernel/devman.h>
#include <kernel/liminetty.h>
#include <errno.h>
#include <arch/panic.h>

#define CONSOLE_MAJOR 1

static int isatty(int minor){
	return 0;
}

static int read(int *error, int dev, void* buff, size_t count, size_t offset){
	*error = ENOSYS;
	return -1;
}

static int write(int *error, int dev, void* buff, size_t count, size_t offset){
	*error = 0;
	
	liminetty_writeuser(buff, count);
	
	return count;
}

devcalls calls = {
	read, write, isatty
};


void consoledev_init(){
	
	if(devman_newdevice("console", TYPE_CHARDEV, CONSOLE_MAJOR, 0, &calls))
	_panic("Failed to create console device", 0);


}
