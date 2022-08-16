#include <arch/panic.h>
#include <arch/idt.h>
#include <arch/regs.h>
#include <stdio.h>
#include <stddef.h>
#include <arch/smp.h>

#define TRACE_MAXDEPTH 10

static void tracestack(uint64_t** addr){
	for(size_t depth = 0; depth < TRACE_MAXDEPTH; ++depth){

		uint64_t *calleeaddr = *(addr + 1);
		addr = (uint64_t**)*addr;

		printf("%lu: %p\n", depth, calleeaddr);

		if(depth == TRACE_MAXDEPTH || calleeaddr == 0) break;
	}
}

__attribute((noreturn)) void _panic(char* reason, arch_regs *reg){

	printf("PANIC: %s\n", reason);

	// XXX maybe this should be an NMI?
	arch_smp_sendipi(0, VECTOR_PANIC, IPI_CPU_ALLBUTSELF);	
	
	if(reg){
		printf("Register dump:\nRAX: %p RBX: %p RCX: %p RDX: %p R8: %p R9: %p R10: %p R11: %p R12: %p R13: %p R14: %p R15: %p RDI: %p RSP: %p RBP: %p DS: %p ES: %p FS: %p GS: %p CR2: %p ERR: %p RIP: %p CS: %p RFLAGS: %p SS: %p\n",
		reg->rax, reg->rbx, reg->rcx, reg->rdx, reg->r8, reg->r9, reg->r10,
		reg->r11, reg->r12, reg->r13, reg->r14, reg->r15, reg->rdi, reg->rsp, reg->rbp, reg->ds, reg->es, reg->fs, reg->gs, reg->cr2, reg->error, reg->rip, reg->cs, reg->rflags, reg->ss
		);

		printf("Panic location: %p\n", reg->rip);
		
		if(reg->rbp != 0 && reg->cs == 0x28){
			printf("Stack trace:\n");
			tracestack((uint64_t**)reg->rbp);
		}

	}
	

	while(1){
		asm("cli;hlt;");
	}

}
