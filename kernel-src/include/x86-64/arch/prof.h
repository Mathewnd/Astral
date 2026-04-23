#ifndef ARCH_PROF_H
#define ARCH_PROF_H

#include <arch/context.h>
#include <stdbool.h>

bool arch_profiling_irq(context_t *ctx);
void arch_profiling_init(void);

#endif
