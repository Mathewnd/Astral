#include <kernel/syscalls.h>
#include <kernel/vmm.h>
#include <sys/types.h>
#include <errno.h>
#include <arch/cls.h>

// linux abi

#define PROT_NONE  0x00
#define PROT_READ  0x01
#define PROT_WRITE 0x02
#define PROT_EXEC  0x04

#define MAP_FILE    0x00
#define MAP_SHARED    0x01
#define MAP_PRIVATE   0x02
#define MAP_FIXED     0x10
#define MAP_ANON      0x20
#define MAP_ANONYMOUS 0x20
#define MAP_NORESERVE 0x4000

#define MS_ASYNC 0x01
#define MS_INVALIDATE 0x02
#define MS_SYNC 0x04

#define MCL_CURRENT 0x01
#define MCL_FUTURE 0x02

#define POSIX_MADV_NORMAL 0
#define POSIX_MADV_RANDOM 1
#define POSIX_MADV_SEQUENTIAL 2
#define POSIX_MADV_WILLNEED 3
#define POSIX_MADV_DONTNEED 4

#define MADV_NORMAL 0
#define MADV_RANDOM 1
#define MADV_SEQUENTIAL 2
#define MADV_WILLNEED 3
#define MADV_DONTNEED 4
#define MADV_FREE 8

#define MREMAP_MAYMOVE 1
#define MREMAP_FIXED 2

#define MFD_CLOEXEC 1U
#define MFD_ALLOW_SEALING 2U

syscallret syscall_mmap(void* hint, size_t len, int prot, int flags, int ifd, off_t offset){

	syscallret retv;
	
	retv.ret = -1;
	retv.errno = 0;

	if(len == 0 || hint > USER_SPACE_END){
		retv.errno = EINVAL;
		return retv;
	}
	
	size_t mmuflags = 0;

	if(prot & PROT_READ)
		mmuflags |= ARCH_MMU_MAP_READ;
	
	if(prot & PROT_WRITE)
		mmuflags |= ARCH_MMU_MAP_WRITE;
	
	if(!prot & PROT_EXEC)
		mmuflags |= ARCH_MMU_MAP_NOEXEC;
	
	mmuflags |= ARCH_MMU_MAP_USER;

	size_t plen = len / PAGE_SIZE + (len % PAGE_SIZE ? 1 : 0);

	void* ret = NULL;

	if(flags & MAP_ANON){

		if(hint)
			ret = vmm_allocnowat(hint, mmuflags, len) ? hint : NULL; // TODO demand page
		if(ret == NULL && (flags & MAP_FIXED) == 0)
			ret = vmm_allocfrom(USER_ALLOC_START, mmuflags, plen);

		if(!ret)
			retv.errno = ENOMEM;
		else{
			retv.ret = ret;
			if(hint && (flags & MAP_ANON))
				memset(ret, 0, plen*PAGE_SIZE);
		}
	}
	else{
		
		// open node
		
		proc_t* proc = arch_getcls()->thread->proc;

		fd_t* fd;

        	retv.errno = fd_access(&proc->fdtable, &fd, ifd);

		if(retv.errno)
			return retv;


		if(hint)
			_panic("mmap: file hint unimplemented", 0);

		ret = vmm_allocfrom(USER_ALLOC_START, mmuflags, plen);

		if(!ret)
			retv.errno = ENOMEM;
		else{
			retv.errno = vmm_mapfile(fd->node, ret, plen, offset, mmuflags);
			if(!retv.errno)
				retv.ret = ret;
		}

		fd_release(fd);
		
	}
	
	return retv;

}
