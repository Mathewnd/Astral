#include <kernel/syscalls.h>
#include <kernel/sched.h>
#include <arch/cls.h>
#include <kernel/fd.h>
#include <sys/types.h>
#include <arch/spinlock.h>
#include <errno.h>
#include <kernel/alloc.h>
#include <stddef.h>


syscallret syscall_open(const char* pathname, int flags, mode_t mode){

	syscallret retv;
	retv.ret = -1;

	if(pathname > USER_SPACE_END){
		retv.errno = EFAULT;
		return retv;
	}

	const char* name = alloc(strlen(pathname) + 1); // XXX use an user specific strlen func
	if(!name){
		retv.errno = ENOMEM;
		return retv;
	}

	strcpy(name, pathname); // XXX use an user specific strcpy

	proc_t* proc = arch_getcls()->thread->proc;
	
	spinlock_acquire(&proc->fdlock);

	fd_t* fd = NULL;
	
	// find fd to use
	
	for(uintmax_t i = 0; i < proc->fdcount; ++i){
		spinlock_acquire(&proc->fds[i].lock);
		if(!proc->fds[i].node){
			fd = &proc->fds[i].node;
			break;
		}
		spinlock_release(&proc->fds[i].lock);
	}

	// resize table

	if(!fd){
		
		fd_t* tmp = realloc(proc->fds, sizeof(fd_t)*proc->fdcount + 1);
		
		if(!tmp){
			spinlock_release(&proc->fdlock);
			retv.errno = ENOMEM;
			return retv;
		}

		proc->fds = tmp;
		
		fd = &proc->fds[proc->fdcount];
		++proc->fdcount;
		spinlock_acquire(&fd->lock);

	}

	spinlock_release(&proc->fdlock);
	
	fd->flags = flags + 1; // 1 is added to make O_RDONLY etc easier to lookup
	fd->offset = 0;
	
	vnode_t* file;

	size_t ret = vfs_open(&file, 
		*name == '/' ? proc->root : proc->cwd,
		*name == '/' ? name + 1 : name
	);

	if(ret == ENOENT && (flags & O_CREAT)){
		printf("O_CREAT NOT SUPPORTED YET!\n");
		retv.errno = ENOENT;
		goto _fail;
	}
	else if(ret){
		retv.errno = ret;
		goto _fail;
	}
	
	if((flags & O_DIRECTORY) && (file->st.st_mode & MAKETYPE(TYPE_DIR)) == 0){
		retv.errno = ENOTDIR;
		goto _fail;
	}

	
	fd->node = file;

	spinlock_release(&fd->lock);

	free(name);	
	retv.errno = 0;
	retv.ret = ((uintptr_t)fd - (uintptr_t)proc->fds) / sizeof(fd_t);

	return retv;

	_fail:

	if(file)
		vfs_close(file);
	spinlock_release(&fd->lock);
	fd->node = NULL;
	free(name);
	return retv;

}
