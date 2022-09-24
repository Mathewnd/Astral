#include <kernel/devman.h>
#include <errno.h>
#include <string.h>
#include <arch/panic.h>

static int read(int* error, int minor, void* buff, size_t count, size_t offset){
	*error = 0;
	memset(buff, 0, count);
	return count;
}
static int write(int* error, int minor, void* buff, size_t count, size_t offset){
	*error = ENOSPC;
	return -1;
}

static int isatty(int minor){
	return ENOTTY;
}

static devcalls calls = {
	read, write, isatty
};

void fulldev_init(){
	if(devman_newdevice("full", TYPE_CHARDEV, MAJOR_FULL, 0, &calls)){
		_panic("/dev/full init failed", NULL);
	}
}
