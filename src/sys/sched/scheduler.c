#include <kernel/sched.h>
#include <arch/cls.h>

thread_t* queue;
thread_t* blocked;

static thread_t* next_thread(){
	
	thread_t* thread = queue;

	while(1){

		while(thread != NULL && thread->state != THREAD_STATE_WAITING)
			thread = thread->next;

		if(!thread)
			asm("hlt"); // wait until something happens that would allow a thread to be scheduled
	
	}

	return thread;
	
}

__attribute__((noreturn)) static void sched_switch(thread_t* thread){
	
	thread_t* current = arch_getcls()->thread;

	if(current->proc != thread->proc){

		// change address spaces and all
		
	}
	
	arch_switchcontext(thread->regs);

	__builtin_unreachable();
}

__attribute__((noreturn)) void sched_timerhook(arch_regs* c){
	
	thread_t* current = arch_getcls()->thread;

	memcpy(current->regs, c, sizeof(arch_regs));
	
	;

	__builtin_unreachable();
}
