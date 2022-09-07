#include <ringbuffer.h>
#include <errno.h>
#include <arch/spinlock.h>
#include <kernel/alloc.h>

int ringbuffer_init(ringbuffer_t* ringbuffer, size_t size){
	
	ringbuffer->data = alloc(size);
	
	if(!ringbuffer->data)
		return ENOMEM;

	ringbuffer->size = size;

	return 0;
}

size_t ringbuffer_read(ringbuffer_t* ringbuffer, void* buffer, size_t count){
	
	spinlock_acquire(&ringbuffer->lock);
	
	size_t readc = 0;

	for(;readc < count; ++readc){
		if(ringbuffer->read == ringbuffer->write)
			break;

		uintmax_t readoffset = ringbuffer->read % ringbuffer->size;
		uint8_t* buf = buffer + readc;
		uint8_t* rbuf = ringbuffer->data + readoffset;

		*buf = *rbuf;

		++ringbuffer->read;
		
	}


	spinlock_release(&ringbuffer->lock);
	
	return readc;

}

size_t ringbuffer_write(ringbuffer_t* ringbuffer, void* buffer, size_t count){
	
	spinlock_acquire(&ringbuffer->lock);
	
	size_t writec = 0;
	
	for(;writec < count; ++writec){
		if(ringbuffer->write == ringbuffer->read + ringbuffer->size)
			break;
		
		uintmax_t writeoffset = ringbuffer->write % ringbuffer->size;
		uint8_t* buf = buffer + writec;
		uint8_t* wbuf = ringbuffer->data + writeoffset;

		*wbuf = *buf;
		
		++ringbuffer->write;

	}
	
	spinlock_release(&ringbuffer->lock);

	return writec;

}
