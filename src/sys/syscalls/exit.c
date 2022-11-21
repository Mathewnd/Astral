#include <kernel/sched.h>
#include <arch/cls.h>
#include <arch/spinlock.h>
#include <stdbool.h>
#include <kernel/event.h>
#include <kernel/alloc.h>
#include <arch/interrupt.h>

void syscall_exit(int status){
	proc_t* proc = arch_getcls()->thread->proc;
	

	proc_t* init = sched_getinit();

	spinlock_acquire(&init->lock);
	spinlock_acquire(&proc->lock);
	
	// TODO make all other threads exit

	free(proc->threads);

	for(uintmax_t fd = 0; fd < proc->fdtable.fdcount; ++ fd)
		fd_free(&proc->fdtable, fd);
	


	proc->state = PROC_STATE_ZOMBIE;
	proc->status = status;

	vfs_close(proc->root);	
	vfs_close(proc->cwd);	

	proc_t* child = proc->child;
	proc_t* firstchild = child;

	while(child){
		spinlock_acquire(&child->lock);
		child->parent = init;	
		proc_t* oldc = child;
		proc_t* oldsibling = oldc->sibling;
		if(!child->sibling){
			child->sibling = init->child;
			init->child = firstchild;
		}
		
		child = oldsibling;
		spinlock_release(&oldc->lock);
	}	

	event_signal(&proc->parent->childevent, true);

	arch_interrupt_disable();

	spinlock_release(&proc->lock);
	spinlock_release(&init->lock);

	sched_dequeue();
	
	__builtin_unreachable();

}
