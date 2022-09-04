#include <kernel/event.h>
#include <arch/interrupt.h>
#include <arch/spinlock.h>
#include <arch/cls.h>

static bool addtoevent(thread_t* thread, event_t* event){
	
	spinlock_acquire(&event->lock);

	uintmax_t i = 0;

	for(; i < EVENT_MAX_THREADS; ++i){
		if(!event->threads[i]){
			event->threads[i] = arch_getcls()->thread;
			break;
		}
	}
	
	spinlock_release(&event->lock);

	return true;
	
}

void removefromevent(thread_t* thread, event_t* event){
	spinlock_acquire(&event->lock);
	
	for(size_t i = 0; i < EVENT_MAX_THREADS; ++i){
		if(event->threads[i] == thread){
			event->threads[i] = NULL;
			break;
		}
	}
	
	spinlock_release(&event->lock);
}

int event_wait(event_t* event, bool interruptible){
	
	
	arch_interrupt_disable();

	thread_t* thread = arch_getcls()->thread;

	if(interruptible){
		addtoevent(thread, &thread->sigevent);
	}

	addtoevent(thread, event);

	
	// ...
	
	
	int ret = 0;
	
	event_t* awokenby = thread->awokenby;

	if(awokenby == &thread->sigevent){
		ret = EINTR;
		removefromevent(thread, awokenby);
		awokenby = event;
	}

	removefromevent(thread, awokenby);

	spinlock_release(&event->lock);
	
	arch_interrupt_enable();

	return ret;

}

int event_signal(event_t* event, bool interruptson){
	
	if(interruptson)
		arch_interrupt_disable();
	
	spinlock_acquire(&event->lock);
	
	
	for(uintmax_t i = 0; i < EVENT_MAX_THREADS; ++i){
		
		thread_t* thread = event->threads[i];

		if(!thread)
			continue;
		
		// ...

		
	}
	
	
	spinlock_release(&event->lock);

	if(interruptson)
		arch_interrupt_enable();

}
