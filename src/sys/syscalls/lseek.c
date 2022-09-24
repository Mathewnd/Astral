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

        fd_t* fd;
	
	int ret = fd_access(&proc->fdtable, &fd, ifd); 

	if(ret){
		retv.errno = ret;
		return retv;
	}

	int type = GETTYPE(fd->node->st.st_mode);

	if(type == TYPE_SOCKET || type == TYPE_FIFO){
		retv.errno = ESPIPE;
		goto _ret;
	}

	off_t newoffset = fd->offset;
	size_t fsize = fd->node->st.st_size;
	
	if(type == TYPE_CHARDEV || type == TYPE_BLOCKDEV){
		int err = devman_isseekable(fd->node->st.st_rdev, &fsize);
		if(err){
			retv.errno = err;
			goto _ret;
		}
	}

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
			if(offset == 0){
				retv.errno = 0;
				retv.ret = fd->offset;
				goto _ret;
			}
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
	fd_release(fd);
	return retv;

}
