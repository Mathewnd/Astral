#include <kernel/devman.h>
#include <kernel/block.h>
#include <errno.h>
#include <stdint.h>
#include <stddef.h>
#include <kernel/alloc.h>
#include <arch/spinlock.h>
#include <string.h>
#include <poll.h>

static int lock;
static size_t devcount;
static blockdesc_t* blockdevs;

static int isseekable(int dev, size_t* seekmax){

	if(dev >= devcount)
		return ENODEV;

	spinlock_acquire(&lock);

	blockdesc_t desc = blockdevs[dev];

	spinlock_release(&lock);
	
	*seekmax = desc.capacity * desc.blocksize;

	return 0;

}
static int poll(int dev, pollfd* fd){
	
	if(fd->events & POLLOUT)
		fd->revents |= POLLIN;
	
	if(fd->events & POLLOUT)
		fd->revents |= POLLOUT;

}

int block_read(int* error, int dev, void* buffer, size_t count, size_t offset){

	//printf("block_read called: buffer %p count %lu offset %lu\n", buffer, count, offset);	

	if(dev >= devcount){
		*error = ENODEV;
		return -1;
	}

	*error = 0;

	if(count == 0)
		return 0;


	spinlock_acquire(&lock);

	blockdesc_t desc = blockdevs[dev];

	spinlock_release(&lock);

	uintmax_t startlba = offset / desc.blocksize;

	if(startlba >= desc.capacity)
		return 0;

	uintmax_t lbaoffset = offset % desc.blocksize;
	size_t countremainder = count % desc.blocksize;
	size_t extrabytes = lbaoffset + countremainder;
	size_t extralba = extrabytes / desc.blocksize + 1;
	size_t lbacount = count / desc.blocksize + extralba;
	uintmax_t lbatop = startlba + lbacount;
	size_t lbadiff = 0;
	
	//printf("startlba: %lu countremainder: %lu extrabytes: %lu extralba: %lu lbacount: %lu lbatop: %lu lbadiff: %lu\n", startlba, countremainder, extrabytes, extralba, lbacount, lbatop, lbadiff);

	if(lbatop >= desc.capacity){
		lbadiff += lbatop - desc.capacity;
		lbatop  -= lbatop - desc.capacity + 1;
	}
	
	//printf("startlba: %lu countremainder: %lu extrabytes: %lu extralba: %lu lbacount: %lu lbatop: %lu lbadiff: %lu\n", startlba, countremainder, extrabytes, extralba, lbacount, lbatop, lbadiff);

	if(lbadiff){
		
		count -= countremainder;
		--lbacount;

		if(lbadiff > 1){	
			--lbadiff;

			lbacount -= lbadiff;
			count -= lbadiff * desc.blocksize;
		}
	}
	
	//printf("startlba: %lu countremainder: %lu extrabytes: %lu extralba: %lu lbacount: %lu lbatop: %lu lbadiff: %lu\n", startlba, countremainder, extrabytes, extralba, lbacount, lbatop, lbadiff);

	if(lbacount == 0)
		return 0;
	
	uint8_t* lbabuffer = alloc(lbacount*desc.blocksize);

	if(!lbabuffer){
		*error = ENOMEM;
		return -1;
	}
	
	*error = desc.calls.read(desc.internal, lbabuffer, startlba, lbacount);
	
	memcpy(buffer, (uint8_t*)lbabuffer + lbaoffset, count);
	
	if(*error)
		return -1;


	return count;

}


static devcalls calls = {
	.read = block_read
};

int block_registernew(blockdesc_t* desc, char* name){

	spinlock_acquire(&lock);

	int status = 0;

	if(!blockdevs){
		blockdevs = alloc(sizeof(blockdesc_t));
		if(!blockdevs)
			status = ENOMEM;
	}
	else{
		void* tmp = realloc(blockdevs, sizeof(blockdesc_t)*(devcount+1));
		if(!tmp)
			status = ENOMEM;
		else
			blockdevs = tmp;
	}
	
	if(status){
		spinlock_release(&lock);
		return status;
	}

	blockdevs[devcount] = *desc;

	status = devman_newdevice(name, TYPE_BLOCKDEV, MAJOR_BLOCK, devcount, &calls); 

	++devcount;

	spinlock_release(&lock);
	return status;

}

