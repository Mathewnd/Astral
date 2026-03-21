#ifndef _GDT_H
#define _GDT_H

#include <stddef.h>

void arch_gdt_reload(void);

// should be called with preemption DISABLED
void arch_gdt_set_ldt(void *base, size_t limit);

#endif
