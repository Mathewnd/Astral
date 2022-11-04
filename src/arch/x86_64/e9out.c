#include <kernel/devman.h>
#include <arch/e9.h>
#include <sys/stat.h>

#include <stddef.h>
#include <stdint.h>

static int write(int* error, int minor, void* buff, size_t count, size_t offset){
	
	char* b = buff;

	for(uintmax_t i = 0; i < count; ++i)
		e9_putc(b[i]);

	*error = 0;

	return count;

}


static devcalls calls = {
	.write = write
};

void e9out_init(){
	devman_newdevice("e9out", TYPE_CHARDEV, MAJOR_E9OUT, 0, &calls);
}
