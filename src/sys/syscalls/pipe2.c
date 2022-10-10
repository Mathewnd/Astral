#include <kernel/syscalls.h>
#include <kernel/vfs.h>
#include <arch/cls.h>
#include <kernel/pipe.h>

extern fs_t kerneltmpfs;

// the pipe2 system call will return the two fds in the same register

syscallret syscall_pipe2(int flags){
	syscallret retv;
	retv.ret = -1;
	
	proc_t* proc = arch_getcls()->thread->proc;

	fd_t* rfd = NULL;
	fd_t* wfd = NULL;

	int rifd, wifd;

	retv.errno = fd_alloc(&proc->fdtable, &rfd, &rifd, 0);

        if(retv.errno)
                return retv;
        
	retv.errno = fd_alloc(&proc->fdtable, &wfd, &wifd, 0);

        if(retv.errno)
                goto _fail;
        
	wfd->flags = O_WRONLY+1;
	rfd->flags = O_RDONLY+1;

	wfd->node = vfs_newnode("FIFO", &kerneltmpfs, NULL);
	if(!wfd->node){
		retv.errno = ENOMEM;
		goto _fail;
	}
	rfd->node = wfd->node;
	
	rfd->mode = MAKETYPE(TYPE_FIFO);
	wfd->mode = rfd->mode;
	rfd->node->st.st_mode = rfd->mode;
	rfd->node->refcount = 2;
	
	pipe_t* pipe = pipe_create(PAGE_SIZE*16);

	if(!pipe){
		retv.errno = ENOMEM;
		goto _fail;
	}

	wfd->node->objdata = pipe;

	pipe->readers = 1;
	pipe->writers = 1;

	retv.errno = 0;
	retv.ret = ((long)rifd & 0xFFFFFFFF) + (((long)wifd & 0xFFFFFFFF) << 32);

	fd_release(wfd);
	fd_release(rfd);

	return retv;

	_fail:
		
	if(wfd){
		fd_release(wfd);	
		fd_free(&proc->fdtable, wifd);
	}
	if(rfd){
		fd_release(rfd);
		fd_free(&proc->fdtable, rifd);
	}
	
	return retv;
	

}
