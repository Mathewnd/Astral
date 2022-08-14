#include <arch/regs.h>

void arch_regs_setupkernel(arch_regs* regs, void* ip, void* stack, bool interrupts){
        regs->rip = ip;
        regs->rsp = stack;
        regs->cs = 0x28;
        regs->ds = 0x30;
        regs->es = 0x30;
        regs->fs = 0x30;
        regs->gs = 0x30;
        regs->ss = 0x30;
        regs->rflags = interrupts ? 0x200 : 0;	
}

void arch_regs_setupuser(arch_regs* regs, void* ip, void* stack, bool interrupts){
	regs->rip = ip;
	regs->rsp = stack;
	regs->cs = 0x38 | 3;
	regs->ds = 0x40 | 3;
        regs->es = 0x40 | 3;
        regs->fs = 0x40 | 3;
        regs->gs = 0x40 | 3;
        regs->ss = 0x40 | 3;
        regs->rflags = interrupts ? 0x200 : 0;

}
