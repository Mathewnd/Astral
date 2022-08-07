#include <kernel/devman.h>
#include <kernel/vfs.h>
#include <kernel/devfs.h>
#include <stdio.h>
#include <kernel/alloc.h>
#include <sys/sysmacros.h>
#include <arch/panic.h>
#include <sys/stat.h>

static devcalls** majorcalls;
static size_t highestmajor = 0;

int devman_newdevice(char* name, int type, int major, int minor, devcalls* calls){
	
	if(major > highestmajor){
		void* addr = realloc(majorcalls, major*sizeof(devcalls*)+1);
		if(!addr)
			return ENOMEM;
		majorcalls = addr;
	}
	
	majorcalls[major] = calls;

	return devfs_newdevice(name, type, makedev(major, minor), 0777);
	
}

int devman_write(int *error, int dev, void* buff, size_t count, size_t offset){
	
	if(major(dev) > highestmajor)
		return ENODEV;
	

	return majorcalls[major(dev)]->write(error, minor(dev), buff, count, offset);
	
	
}

int devman_read(int *error, int dev, void* buff, size_t count, size_t offset){
	
	if(major(dev) > highestmajor)
		return ENODEV;
	

	return majorcalls[major(dev)]->read(error, minor(dev), buff, count, offset);


}

void devman_init(){
	
	devfs_init();

	size_t err = vfs_mkdir(vfs_root(), "dev", 0777);
	if(err && err != EEXIST){
		printf("Failed to mkdir /dev: %lu\n", err);
		_panic("Devman initialization failed", 0);
	}

	err = vfs_mount(vfs_root(), NULL, "dev", "devfs", 0, NULL);

	if(err){
		printf("Failed to mount devfs in /dev: %lu\n", err);
		_panic("Devman initialization failed", 0);
	}
	
	printf("devfs mounted in /dev\n");

	majorcalls = alloc(sizeof(devcalls*));


}
