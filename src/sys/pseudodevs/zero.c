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
	*error = 0;
	return count;
}

static int isatty(int minor){
	return ENOTTY;
}

static devcalls calls = {
	read, write, isatty
};

void zerodev_init(){
	if(devman_newdevice("zero", TYPE_CHARDEV, MAJOR_ZERO, 0, &calls)){
		_panic("/dev/zero init failed", NULL);
	}
}
