#include <kernel/syscalls.h>
#include <kernel/fd.h>
#include <arch/cls.h>
#include <kernel/vfs.h>
#include <arch/spinlock.h>

syscallret syscall_close(int ifd){

        syscallret retv;
        retv.ret = -1;

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
	
	int ret = vfs_close(fd->node);
	
	if(ret){
		retv.errno = ret;
		goto _ret;
	}

	// the fd table isn't resized on a close
	// although that can be added if it becomes an issue
	
	fd->node = NULL;

	retv.ret = 0;
	retv.errno = 0;

	_ret:

	spinlock_release(&fd->lock);
	return retv;
}
