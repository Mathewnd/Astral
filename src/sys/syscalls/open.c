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
	
	int ifd;
	fd_t* fd;

	int err = fd_alloc(&proc->fdtable, &fd, &ifd, 0);
	
	if(err){
		retv.errno = err;
		free(name);
		return retv;	
	}

	fd->flags = flags + 1; // 1 is added to make O_RDONLY etc easier to lookup
	fd->offset = 0;
	
	vnode_t* file = NULL;
	
	retry:

	size_t ret = vfs_open(&file, 
		*name == '/' ? proc->root : proc->cwd,
		*name == '/' ? name + 1 : name
	);

	if(ret == ENOENT && (flags & O_CREAT)){
		ret = vfs_create(
			*name == '/' ? proc->root : proc->cwd,
			name, mode);
		
		if(ret){
			retv.errno = ret;
			goto _fail;
		}

		flags ^= O_CREAT; // remove O CREAT from flags
		
		goto retry;

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

	fd_release(fd);
	
	free(name);	
	retv.errno = 0;
	retv.ret = ifd;

	return retv;

	_fail:

	if(file)
		vfs_close(file);

	fd_release(fd);
	fd_free(&proc->fdtable,  fd);
	fd->node = NULL;
	free(name);
	return retv;

}
