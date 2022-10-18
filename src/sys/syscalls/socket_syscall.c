#include <kernel/syscalls.h>
#include <kernel/socket.h>
#include <kernel/fd.h>
#include <arch/cls.h>

extern fs_t kerneltmpfs;

syscallret syscall_socket(int domain, int type, int protocol){
	
	syscallret retv;
	retv.ret = -1;

	proc_t* proc = arch_getcls()->thread->proc;

	fd_t* fd = NULL;
	int   ifd;

	retv.errno = fd_alloc(&proc->fdtable, &fd, &ifd, 0);

	if(retv.errno)
		return retv;

	fd->node = vfs_newnode("SOCKET", &kerneltmpfs, NULL);
	if(!fd->node){
		retv.errno = ENOMEM;
		goto _fail;
	}
	fd->mode = 0777 | MAKETYPE(TYPE_SOCKET);
	fd->node->refcount = 1;
	fd->node->st.st_mode = fd->mode;

	socket_t* sock;

	retv.errno = socket_new(&sock, domain, type, protocol);
	
	if(retv.errno)
		goto _fail;
	
	fd->node->objdata = sock;

	fd_release(fd);

	retv.ret = ifd;
	return retv;
	
	_fail:
	
	if(fd){
		fd_release(fd);
		fd_free(&proc->fdtable, ifd);
	}

	return retv;

}
