#include <kernel/syscalls.h>
#include <kernel/vmm.h>
#include <errno.h>
#include <arch/spinlock.h>
#include <kernel/sched.h>
#include <arch/cls.h>
#include <arch/regs.h>

syscallret syscall_fork(arch_regs* ctx){
	
	syscallret retv;
	retv.ret = -1;
	thread_t* newthread = sched_newuthread(NULL, THREAD_DEFAULT_KSTACK_SIZE, NULL, NULL, false, THREAD_PRIORITY_USER);
	
	if(!newthread){
		retv.errno = ENOMEM;
		return retv;
	}
	
	proc_t* newproc = newthread->proc;
	thread_t* thread = arch_getcls()->thread;
	proc_t* proc = thread->proc;

	spinlock_acquire(&proc->lock);		

	fd_tableclone(&proc->fdtable, &newproc->fdtable);

	int err = vmm_fork(thread->ctx, newthread->ctx);

	if(err){
		vmm_destroy(newthread->ctx);
		free(newthread->regs);
		free(newthread->kernelstackbase);
		free(newthread);
		retv.errno = err;
		return retv;
	}
	
	newthread->tid = newproc->pid;

	newproc->parent = proc;
	newproc->sibling = proc->child;
       	proc->child = newproc;	
	newproc->gid = proc->gid;
	newproc->uid = proc->uid;
	newproc->umask = proc->umask;

	vfs_acquirenode(proc->root);
	vfs_acquirenode(proc->cwd);

	newproc->root = proc->root;
	newproc->cwd  = proc->cwd;

	memcpy(newthread->regs, ctx, sizeof(arch_regs));
	arch_regs_saveextra(&newthread->extraregs);

	arch_regs_setret(newthread->regs, 0);
	arch_regs_seterrno(newthread->regs, 0);

	spinlock_release(&proc->lock);

	sched_queuethread(newthread);

	retv.errno = 0;
	retv.ret = newproc->pid;
	return retv;

}
