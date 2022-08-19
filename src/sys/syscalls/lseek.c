#include <kernel/syscalls.h>
#include <arch/cls.h>
#include <kernel/fd.h>
#include <arch/spinlock.h>
#include <sys/stat.h>

#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2

syscallret syscall_lseek(int ifd, off_t offset, int whence){

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
	
	int type = GETTYPE(fd->node->st.st_mode);

	if(type == TYPE_SOCKET || type == TYPE_FIFO){
		retv.errno = ESPIPE;
		goto _ret;
	}

	off_t newoffset = fd->offset;
	size_t fsize = fd->node->st.st_size;

	switch(whence){
		case SEEK_SET:
			if(offset < 0 || offset > fsize){
				retv.errno = EINVAL;
				goto _ret;
			}
			newoffset = offset;
			break;
		case SEEK_CUR:
			newoffset += offset;
			if(newoffset < 0 || newoffset > fsize){
				retv.errno = EINVAL;
				goto _ret;
			}
			
			if(newoffset < offset){
				retv.errno = EOVERFLOW;
				goto _ret;
			}
			break;
		case SEEK_END:
			newoffset = fsize;
			break;
		default:
			retv.errno = EINVAL;
			goto _ret;
	}
	
	fd->offset = newoffset;
	retv.errno = 0;
	retv.ret = newoffset;

	_ret:
	spinlock_release(&fd->lock);
	return retv;

}
