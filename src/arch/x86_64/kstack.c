#include <arch/kstack.h>
#include <arch/ist.h>
#include <arch/cls.h>

void arch_setkernelstack(void* stack){
	
	arch_getcls()->ist.rsp0 = (uint64_t)stack;
}
