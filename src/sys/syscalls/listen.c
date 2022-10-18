#include <kernel/syscalls.h>
#include <kernel/fd.h>
#include <arch/cls.h>
#include <errno.h>
#include <sys/stat.h>
#include <kernel/socket.h>

syscallret syscall_listen(int sockfd, int backlog){
	
	syscallret retv;
	retv.ret = -1;

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

	retv.errno = socket_listen(fd->node->objdata, backlog);
	
	_ret:

	fd_release(fd);

	return retv;

}
