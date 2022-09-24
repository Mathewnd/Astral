#include <kernel/devman.h>
#include <limine.h>
#include <string.h>
#include <errno.h>

static volatile struct limine_framebuffer_request fbreq = {
	.id = LIMINE_FRAMEBUFFER_REQUEST,
	.revision = 0
};

static struct limine_framebuffer** fbs;
static size_t fbcount;

static int devchecks(int fb, size_t* count, size_t offset){
	
	if(fb >= fbcount)
		return ENODEV;

	size_t end = fbs[fb]->pitch * fbs[fb]->height;

	if(offset >= end){
		*count = 0;
	}

	if(*count + offset > end)
		*count = end - offset;

	return 0;
	

}

int read(int* error, int minor, void* buff, size_t count, size_t offset){

	*error = devchecks(minor, &count, offset);

	if(*error)
		return -1;

	memcpy(buff, fbs[minor]->address + offset, count);

	return count;
	
}

int write(int* error, int minor, void* buff, size_t count, size_t offset){
	
	*error = devchecks(minor, &count, offset);

	if(*error)
		return -1;

	memcpy(fbs[minor]->address + offset, buff, count);

	return count;
}

int isatty(int minor){
	return ENOTTY;
}

static int isseekable(int minor, size_t* max){
        
	if(minor >= fbcount)
		return ENODEV;

	*max = fbs[minor]->pitch * fbs[minor]->height;

        return 0;
}


static devcalls calls = {
	read, write, isatty, isseekable
};

void fb_init(){
	
	if(!fbreq.response)
		return;
	
	fbcount = fbreq.response->framebuffer_count;
	fbs = fbreq.response->framebuffers;
	
	char name[] = { 'f', 'b', '0', 0};

	for(uintmax_t fb = 0; fb < fbcount; ++fb){
		
		devman_newdevice(name, TYPE_CHARDEV, MAJOR_FB, fb, &calls);

		++name[2]; // increase the fbX num

	}

}
