#include <arch/cls.h>
#include <arch/msr.h>
#include <stdint.h>

cls_t bspcls;
vmm_context initialvmmcontext;

void arch_setcls(cls_t* addr){
	wrmsr(MSR_GSBASE, (uint64_t)addr);
	wrmsr(MSR_KERNELGSBASE, (uint64_t)addr);
}

cls_t* arch_getcls(){
	return (cls_t*)rdmsr(MSR_GSBASE);
}

void bsp_setcls(){
	arch_setcls(&bspcls);
	bspcls.context = &initialvmmcontext;
}
