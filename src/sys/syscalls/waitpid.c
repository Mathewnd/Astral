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

	event_wait(&proc->childevent, true);

	// now find the child
	
	spinlock_acquire(&proc->lock);
	proc_t* child = proc->child;
	proc_t* sibling = NULL;
		
	while(child){
		if(pid > 0 && child->pid != pid)
		       continue;	

		if(child->state == PROC_STATE_ZOMBIE){
			break;
		}

		if(child->sibling)
			sibling = child;
		child = child->sibling;

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

	spinlock_release(&proc->lock);

	*status = child->status; // XXX safer way of doing this
	
	free(child);

	retv.ret = child->pid;
	retv.errno = 0;
	
	return retv;


}
