#include <kernel/syscalls.h>
#include <kernel/sched.h>
#include <arch/cls.h>
#include <arch/spinlock.h>

void syscall_threadexit(){
	
	thread_t* thread = arch_getcls()->thread;

	proc_t* proc = thread->proc;

	// this loop is required in order to prevent a race condition related to syscall_exit()
	
	spinlock_acquire(&proc->threadexitlock);

	bool onlythread = true;

	for(int i = 0; i < proc->threadcount; ++i){
		if(proc->threads[i]->state != THREAD_STATE_DEAD && proc->threads[i] != thread){
			onlythread = false;
			break;
		}
	}
	
	if(onlythread)
		syscall_exit(0); // if it is the only thread, use syscall_exit instead.
		
	sched_die();

}
