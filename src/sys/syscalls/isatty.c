#include <kernel/syscalls.h>
#include <kernel/fd.h>
#include <kernel/vfs.h>
#include <arch/cls.h>
#include <kernel/sched.h>
#include <arch/spinlock.h>

syscallret syscall_isatty(int ifd){
	
	syscallret retv;
        retv.ret = -1;
	
	proc_t* proc = arch_getcls()->thread->proc;

        fd_t* fd = proc->fds + ifd;

        spinlock_acquire(&proc->fdlock);
        
	if(proc->fdcount <= ifd || fd->node == NULL){
                spinlock_release(&proc->fdlock);
                retv.errno = EBADF;
                return retv;
        }

        spinlock_acquire(&fd->lock);
        spinlock_release(&proc->fdlock);

	int ret = vfs_isatty(fd->node);
	
	retv.errno = ret;

	retv.ret = retv.errno == ENOTTY ? 0 : 1;

	spinlock_release(&fd->lock);

	return retv;
	
}
