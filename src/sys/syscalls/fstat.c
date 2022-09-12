#include <kernel/syscalls.h>
#include <sys/stat.h>
#include <kernel/fd.h>
#include <arch/cls.h>
#include <kernel/vfs.h>
#include <arch/spinlock.h>
#include <string.h>

syscallret syscall_fstat(int ifd, stat* st){
	syscallret retv;
	retv.ret = -1;

	if(st > USER_SPACE_END){
		retv.errno = EFAULT;
		return retv;
	}

	proc_t* proc = arch_getcls()->thread->proc;

        fd_t* fd = proc->fds + ifd;

        spinlock_acquire(&proc->fdlock);

        if(proc->fdcount <= ifd || fd->node == NULL || (fd->flags & FD_FLAGS_READ) == 0){
                spinlock_release(&proc->fdlock);
                retv.errno = EBADF;
                return retv;
        }

        spinlock_acquire(&fd->lock);
        spinlock_release(&proc->fdlock);
	
	memcpy(st, &fd->node->st, sizeof(stat)); // XXX user memcpy
	
	spinlock_release(&fd->lock);

	retv.errno = 0;
	retv.ret = 0;

	return retv;

}
