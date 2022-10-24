#include <kernel/syscalls.h>
#include <kernel/fd.h>
#include <arch/panic.h>
#include <kernel/socket.h>
#include <kernel/vmm.h>
#include <arch/cls.h>
#include <kernel/alloc.h>


syscallret syscall_recvmsg(int ifd, msghdr *msg, int flags){

	if(flags)
		_panic("No flags support for recvmsg yet", 0);

	syscallret retv;
        retv.ret = -1;
	
        if(msg > USER_SPACE_END || msg->msg_iov > USER_SPACE_END){
                retv.errno = EFAULT;
                return retv;
        }
	
	// get len of iovecs
		
	size_t totallen = 0;

	for(uintmax_t i = 0; i < msg->msg_iovlen; ++i){
		if(msg->msg_iov[i].iov_base > USER_SPACE_END){
			retv.errno = EFAULT;
			return retv;
		}
		
		totallen += msg->msg_iov[i].iov_len;

	}
	
	// open fd

        proc_t* proc = arch_getcls()->thread->proc;

        fd_t* fd;

        int err = fd_access(&proc->fdtable, &fd, ifd);

        if(err){
                retv.errno = err;
                return retv;
        }

	void* buff = alloc(totallen);

	if(!buff){
		retv.errno = ENOMEM;
		goto _return;
	}
	
	if((!fd->node) || GETTYPE(fd->node->st.st_mode) != TYPE_SOCKET){
		retv.errno = ENOTSOCK;
		goto _return;
	}

	int readcount = socket_recv(fd->node->objdata, buff, totallen, flags, &err, fd);
	
	retv.errno = err;

	if(retv.errno)
		goto _return;

	// now copy the data into the proper locations
	
	void* buffptr = buff;

	if(msg->msg_iov > USER_SPACE_END){
		retv.errno = EFAULT;
		goto _return;
	}

	for(size_t i = 0; i < msg->msg_iovlen; ++i){
		if(msg->msg_iov[i].iov_base > USER_SPACE_END){
			retv.errno = EFAULT;
			goto _return;
		}
		
		size_t iovlen = msg->msg_iov[i].iov_len;
		void* iovbuf = msg->msg_iov[i].iov_base;
		
		memcpy(iovbuf, buffptr, iovlen);

		buffptr += iovlen;
	}

	retv.errno = 0;
	retv.ret = readcount;
	
	_return:

	if(buff)
		free(buff);

	fd_release(fd);
	
	return retv;
	
}
