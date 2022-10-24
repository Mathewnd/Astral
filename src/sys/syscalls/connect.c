#include <kernel/syscalls.h>
#include <kernel/fd.h>
#include <arch/cls.h>
#include <errno.h>
#include <kernel/vmm.h>
#include <sys/stat.h>
#include <kernel/socket.h>

syscallret syscall_connect(int sockfd, void* addr, size_t addrlen){
	
	syscallret retv;
	retv.ret = -1;

        if(addr > USER_SPACE_END){
                retv.errno = EFAULT;
                return retv;
        }

        proc_t* proc = arch_getcls()->thread->proc;

        fd_t* fd;

        int err = fd_access(&proc->fdtable, &fd, sockfd);

        if(err){
                retv.errno = err;
                return retv;
        }
	

	if(GETTYPE(fd->mode) != TYPE_SOCKET){
		retv.errno = ENOTSOCK;
		goto _ret;
	}

	retv.errno = socket_connect(fd->node->objdata, addr, addrlen, fd);
	
	_ret:

	fd_release(fd);

	return retv;

}
