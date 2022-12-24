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

	fd_t* fd;

	int err = fd_access(&proc->fdtable, &fd, ifd);

	if(err){
		retv.errno = err;
		return retv;
	}

        void* kbuff = alloc(count);

        if(!kbuff){
                fd_release(fd);
		retv.errno = ENOMEM;
                return retv;
        }
	
	retv.errno = u_memcpy(kbuff, buff, count);

	if(retv.errno)
		goto _ret;
	
	
	if(fd->flags & O_APPEND)
		fd->offset = fd->node->st.st_size;

	
	size_t writec = vfs_write(&err, fd->node, kbuff, count, fd->offset);


	if(err){
		retv.errno = err;
		goto _ret;
	}

	fd->offset += writec;
	
	retv.errno = 0;
	retv.ret = writec;

	_ret:
	fd_release(fd);
	free(kbuff);
	return retv;
}

