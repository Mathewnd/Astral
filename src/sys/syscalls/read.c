#include <kernel/syscalls.h>
#include <kernel/fd.h>
#include <kernel/alloc.h>
#include <kernel/vfs.h>
#include <arch/cls.h>
#include <kernel/sched.h>
#include <arch/spinlock.h>
#include <kernel/vmm.h>

syscallret syscall_read(int ifd, void* buff, size_t count){
	
	syscallret retv;
	retv.ret = -1;

	if(buff > USER_SPACE_END){
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

	void* kbuff = alloc(count);

	if(!kbuff){
		spinlock_release(&fd->lock);
		retv.errno = ENOMEM;
		return retv;
	}

	int err;
	size_t readc = vfs_read(&err, fd->node, kbuff, count, fd->offset);



	if(err){	
		spinlock_release(&fd->lock);
		free(kbuff);
		retv.errno = err;
		return retv;
	}

	fd->offset += readc;
	
	spinlock_release(&fd->lock);

	memcpy(buff, kbuff, count); // XXX user version for safety

	retv.errno = 0;
	retv.ret = readc;

	free(kbuff);
	
	return retv;
}
