#include <kernel/pipe.h>
#include <kernel/alloc.h>
#include <arch/interrupt.h>
#include <kernel/event.h>
#include <errno.h>
#include <arch/spinlock.h>

pipe_t* pipe_create(size_t buffsize){
	pipe_t* pipe = alloc(sizeof(pipe_t));
	if(!pipe)
		return NULL;
	if(ringbuffer_init(&pipe->buff, buffsize)){
		free(pipe);
		return NULL;
	}

	return pipe;
}

int pipe_read(pipe_t* pipe, void* buff, size_t count, int* error){
	
	spinlock_acquire(&pipe->lock);
	
	int readc = 0;
	*error = 0;
	
	for(;;){
		
		arch_interrupt_disable();
		
		readc = ringbuffer_read(&pipe->buff, buff, count);

		if(readc > 0){
			arch_interrupt_enable();
			event_signal(&pipe->revent, true);
			break;
		}
			
		if(pipe->writers == 0)
			break;

		spinlock_release(&pipe->lock);

		if(event_wait(&pipe->wevent, true)){
			spinlock_acquire(&pipe->lock);
			readc = -1;
			*error = EINTR;
			break;
		}
	
		spinlock_acquire(&pipe->lock);

	}
	
	spinlock_release(&pipe->lock);
	
	return readc;
}

int pipe_write(pipe_t* pipe, void* buff, size_t count, int* error){
	spinlock_acquire(&pipe->lock);
	int writec = 0;
	*error = 0;
	if(pipe->readers == 0){
		*error = EPIPE;
		return -1;
	}

	for(;;){
		
		arch_interrupt_disable();

		writec = ringbuffer_write(&pipe->buff, buff, count);

		if(writec > 0){
			event_signal(&pipe->wevent, true);
			break;
		}
		

		spinlock_release(&pipe->lock);

		if(event_wait(&pipe->revent, true)){
			spinlock_acquire(&pipe->lock);
			*error = EINTR;
			writec = -1;
			break;
		}
		spinlock_acquire(&pipe->lock);

		if(pipe->readers == 0){
			*error = EPIPE;
			break;
		}
	}
		
	spinlock_release(&pipe->lock);

	return writec;
}

int pipe_poll(pipe_t* pipe, pollfd* fd){
	
	spinlock_acquire(&pipe->lock);

	if(fd->events & POLLIN){ // fd is reading
		if(pipe->writers == 0)
			fd->revents |= POLLHUP;
		
		if(pipe->buff.write != pipe->buff.read)
			fd->revents |= POLLIN;

	}
	else { // fd is writing
		if(pipe->readers == 0)
			fd->revents |= POLLERR;
		else if(pipe->buff.write != pipe->buff.read + pipe->buff.size)
			fd->revents |= POLLOUT;
			

	}


	spinlock_release(&pipe->lock);

	return 0;

}
