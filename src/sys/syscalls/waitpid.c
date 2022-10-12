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
			if(pid > 0 && child->pid != pid)
			       continue;	

			if(child->state == PROC_STATE_ZOMBIE){
				loop = false;
				break;
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

	*status = child->status; // XXX safer way of doing this
	
	// the pointer to the last remaining thread is is proc->threads

	thread_t* thread = child->threads;

	
	free(thread->regs);
	free(thread->kernelstackbase);
	vmm_destroy(thread->ctx);
	free(thread);	
	free(child);

	retv.ret = child->pid;
	retv.errno = 0;
	return retv;


}
