#ifndef _SMP_H
#define _SMP_H

#define ARCH_SMP_IPI_TARGET 0
#define ARCH_SMP_IPI_SELF 1
#define ARCH_SMP_IPI_ALL 2
#define ARCH_SMP_IPI_OTHERCPUS 3

#include <arch/cpu.h>

extern size_t arch_smp_cpusawake;
extern cpu_t **smp_cpus;

void arch_smp_wakeup();
void arch_smp_sendipi(cpu_t *targcpu, isr_t *isr, int target, bool nmi);
void arch_smp_haltallothers();

#endif
