#include <kernel/syscalls.h>
#include <kernel/sched.h>
#include <arch/cls.h>
#include <kernel/fd.h>
#include <sys/types.h>
#include <arch/spinlock.h>
#include <errno.h>
#include <kernel/alloc.h>
#include <stddef.h>
#include <string.h>
#include <kernel/ustring.h>

syscallret syscall_openat(int dirfd, const char* pathname, int flags, mode_t mode){
	syscallret retv;
	retv.ret = -1;

	if(pathname > USER_SPACE_END){
		retv.errno = EFAULT;
		return retv;
	}
	
	size_t len;

	retv.errno = u_strlen(pathname, &len);

	if(retv.errno)
		return retv;

	char name[len+1];

	retv.errno = u_strcpy(name, pathname);
	
	if(retv.errno)
		return retv;

	proc_t* proc = arch_getcls()->thread->proc;
	
	int ifd;
	fd_t* fd;

	int err = fd_alloc(&proc->fdtable, &fd, &ifd, 0);
	
	if(err){
		retv.errno = err;
		return retv;	
	}

	fd_t* targfd;
	dirnode_t* target;

	if(dirfd != AT_FDCWD){

		err = fd_access(&proc->fdtable, &targfd, dirfd);

		if(err){
			retv.errno = err;
			fd_release(fd);
			fd_free(&proc->fdtable, fd);
			return retv;
		}

		target = targfd->node;

	}
	else target = proc->cwd;

	fd->flags = flags + 1; // 1 is added to make O_RDONLY etc easier to lookup
	fd->offset = 0;
	
	vnode_t* file = NULL;
	
	retry:

	size_t ret = vfs_open(&file, 
		*name == '/' ? proc->root : target,
		name
	);

	if(ret == ENOENT && (flags & O_CREAT)){
		ret = vfs_create(
			*name == '/' ? proc->root : target,
			name, mode & ~proc->umask);
		
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

	if(GETTYPE(file->st.st_mode) == TYPE_SOCKET){
		retv.errno = ENXIO;
		goto _fail;
	}

	
	fd->node = file;
	fd->mode = file->st.st_mode;

	fd_release(fd);
	
	retv.errno = 0;
	retv.ret = ifd;

	return retv;

	_fail:

	if(file)
		vfs_close(file);

	if(dirfd != AT_FDCWD)
		fd_release(targfd);

	fd_release(fd);
	fd_free(&proc->fdtable,  fd);
	return retv;

}

// kept so no ABI breaks happen

syscallret syscall_open(const char* pathname, int flags, mode_t mode){
	return syscall_openat(AT_FDCWD, pathname, flags, mode);
}
