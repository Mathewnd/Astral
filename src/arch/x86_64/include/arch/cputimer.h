#ifndef _CPUTIMER_H_INCLUDE
#define _CPUTIMER_H_INCLUDE

#include <stddef.h>

size_t arch_cputimer_init();
size_t arch_cputimer_stop();
void arch_cputimer_fire(size_t ticks);

#endif
