#include <kernel/devman.h>
#include <errno.h>
#include <string.h>
#include <arch/panic.h>

static int read(int* error, int minor, void* buff, size_t count, size_t offset){
	*error = 0;
	return 0;
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

void nulldev_init(){
	if(devman_newdevice("null", TYPE_CHARDEV, MAJOR_NULL, 0, &calls)){
		_panic("/dev/null init failed", NULL);
	}
}
