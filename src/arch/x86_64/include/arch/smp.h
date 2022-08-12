#ifndef _SMP_H_INCLUDE
#define _SMP_H_INCLUDE

#include <stddef.h>

void smp_init();

#define IPI_CPU_ALLBUTSELF 3
#define IPI_CPU_ALL 2
#define IPI_CPU_SELF 1
#define IPI_CPU_TARGET 0

void arch_smp_sendipi(int cpu, int vector, int mode);
size_t arch_smp_cpucount();


#endif
