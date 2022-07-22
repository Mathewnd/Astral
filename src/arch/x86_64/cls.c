#include <arch/cls.h>
#include <arch/msr.h>
#include <stdint.h>

cls_t bspcls;


void arch_setcls(void* addr){
	wrmsr(MSR_GSBASE, (uint64_t)addr);
}

void* arch_getcls(){
	return (void*)rdmsr(MSR_GSBASE);
}

void bsp_setcls(){
	arch_setcls(&bspcls);
}
