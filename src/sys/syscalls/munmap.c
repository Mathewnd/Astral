#include <kernel/vmm.h>
#include <kernel/syscalls.h>
#include <arch/cls.h>

syscallret syscall_munmap(void* addr, size_t length){
	
	syscallret retv;
	retv.ret = -1;

	if(length == 0 || length % PAGE_SIZE > 0){
		retv.errno = EINVAL;
		return retv;
	}

	if(addr > USER_SPACE_END){
		retv.errno = EFAULT;
		return retv;
	}
	
	bool res = vmm_unmap(addr, length / PAGE_SIZE);

	retv.ret = 0;
	retv.errno = 0;

	if(!res){
		retv.errno = ENOMEM;
		retv.ret = -1;
	}
	
	return retv;
	
}
