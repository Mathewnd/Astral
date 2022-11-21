#include <kernel/syscalls.h>
#include <kernel/sched.h>
#include <sys/types.h>
#include <kernel/vmm.h>
#include <errno.h>
#include <kernel/event.h>
#include <string.h>
#include <arch/cls.h>
#include <arch/spinlock.h>
#include <kernel/alloc.h>
#include <arch/interrupt.h>

#define UNSUPPORTED "astral: waitpid: Unsupported waitpid options!\n"
#define UNSUPPORTED_PG "astral: waitpid: Process group waits are not supported yet.\n"

syscallret syscall_waitpid(pid_t pid, int *status, int options){
	syscallret retv;
	retv.ret = -1;
	
	if(status > USER_SPACE_END - sizeof(int)){
		retv.errno = EFAULT;
		return retv;
	}

	if(options != 0){
		console_write(UNSUPPORTED, strlen(UNSUPPORTED));
		retv.errno = ENOSYS;
		return retv;
	}
	
	
	if(pid < -1 || pid == 0){
		console_write(UNSUPPORTED_PG, strlen(UNSUPPORTED_PG));
		retv.errno = ENOSYS;
		return retv;
	}


	proc_t* proc = arch_getcls()->thread->proc;

	bool loop = true;
	
	proc_t* child;
	proc_t* sibling;

	while(1){

		// now find the child
		
		spinlock_acquire(&proc->lock);
		child = proc->child;
		sibling = NULL;

		while(child){

			if(child->state == PROC_STATE_ZOMBIE){
				if((pid > 0 && child->pid == pid) || pid == -1){
					loop = false;
					break;
				}
			}

			if(child->sibling)
				sibling = child;
			child = child->sibling;

		}

		spinlock_release(&proc->lock);

		if(loop){
			event_wait(&proc->childevent, true);
			loop = false;
			continue;
		}
		else break;
		
	}


		
	if(!child){
		spinlock_release(&proc->lock);
		retv.errno = ECHILD;
		return retv;
	}

	if(sibling){
		spinlock_acquire(&sibling->lock);
		sibling->sibling = child->sibling;
		spinlock_release(&sibling->lock);
	}
	else{
		proc->child = child->sibling;
	}
	
	if(status)
		*status = child->status; // XXX safer way of doing this
	
	int threadc = proc->threadcount;
	
	// free all the threads

	while(proc->threadcount){
		
		for(int t = 0; t < threadc; ++t){
			
			thread_t* thread = proc->threads[t];
			
			// XXX possible race condition here?

			if(thread->state != THREAD_STATE_DEAD)
				continue;

			free(thread->regs);
			free(thread->kernelstackbase);
			vmm_destroy(thread->ctx);

			thread->state = THREAD_STATE_DESTROYED;

		}
		
		arch_interrupt_disable();

		sched_yield();

		arch_interrupt_enable();

	}

	for(int t = 0; t < threadc; ++t){
		
		thread_t* thread = proc->threads[t];
		
		if(thread->state != THREAD_STATE_DESTROYED)
			_panic("Freeing non-destroyed thread", NULL);

		free(thread);

	}

	free(child);

	retv.ret = child->pid;
	retv.errno = 0;
	return retv;


}
