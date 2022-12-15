#include <kernel/syscalls.h>
#include <dirent.h>
#include <arch/cls.h>
#include <sys/stat.h>
#include <kernel/vmm.h>
#include <kernel/ustring.h>

syscallret syscall_getdirent(int ifd, void* buff, size_t max_size){
	syscallret retv;
	retv.ret = -1;
	
	if(buff > USER_SPACE_END){
		retv.errno = EFAULT;
		return retv;
	}

	size_t readc = max_size / sizeof(dent_t);

	if(readc == 0){
		retv.errno = EINVAL;
		return retv;
	}

	fd_t* fd;

	retv.errno = fd_access(&arch_getcls()->thread->proc->fdtable, &fd, ifd);
	
	if(retv.errno)
		return retv;

	if(GETTYPE(fd->node->st.st_mode) != TYPE_DIR){
		retv.errno = ENOTDIR;
		fd_release(fd);
		return retv;
	}

	dent_t kbuff[readc];

	retv.errno = vfs_getdirent(fd->node, kbuff, readc, fd->offset, &retv.ret);


	fd->offset += retv.ret;
	
	retv.ret *= sizeof(dent_t);

	retv.errno = u_memcpy(buff, kbuff, retv.ret);

	if(retv.errno){
		retv.ret = -1;
		return retv;
	}

	fd_release(fd);

	return retv;

}
