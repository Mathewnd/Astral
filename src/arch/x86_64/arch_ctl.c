#include <kernel/syscalls.h>
#include <arch/msr.h>
#include <errno.h>

#define ARCH_CTL_GSBASE 0
#define ARCH_CTL_FSBASE 1

syscallret syscall_arch_ctl(int func, void* arg){
	
	syscallret retv;

	retv.errno = 0;
	retv.ret = 0;
	
	switch(func){
		case ARCH_CTL_GSBASE:
			wrmsr(MSR_KERNELGSBASE, (uint64_t)arg);
			break;
		case ARCH_CTL_FSBASE:
			wrmsr(MSR_FSBASE, (uint64_t)arg);
			break;
		default:
			retv.errno = EINVAL;

	}

	if(retv.errno)
		retv.ret = -1;

	return retv;

}
