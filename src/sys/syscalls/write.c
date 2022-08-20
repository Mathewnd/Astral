#include <kernel/syscalls.h>
#include <kernel/fd.h>
#include <kernel/alloc.h>
#include <kernel/vfs.h>
#include <arch/cls.h>
#include <kernel/sched.h>
#include <arch/spinlock.h>
#include <kernel/vmm.h>
#include <string.h>

syscallret syscall_write(int ifd, void* buff, size_t count){

	// TODO the other flags

        syscallret retv;
        retv.ret = -1;

        if(buff > USER_SPACE_END){
                retv.errno = EFAULT;
                return retv;
        }

        proc_t* proc = arch_getcls()->thread->proc;

        fd_t* fd = proc->fds + ifd;

        spinlock_acquire(&proc->fdlock);

        if(proc->fdcount <= ifd || fd->node == NULL || (fd->flags & FD_FLAGS_WRITE) == 0){
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
	
	memcpy(kbuff, buff, count); // XXX use a more proper user copy
	
	if(fd->flags & O_APPEND)
		fd->offset = fd->node->st.st_size;

	int err;
	size_t writec = vfs_write(&err, fd->node, kbuff, count, fd->offset);


	if(err){
		retv.errno = err;
		goto _ret;
	}

	fd->offset += writec;
	
	retv.errno = 0;
	retv.ret = writec;

	_ret:
	spinlock_release(&fd->lock);
	free(kbuff);
	return retv;
}

