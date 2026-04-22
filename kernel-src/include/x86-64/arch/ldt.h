#ifndef _LDT_H
#define _LDT_H

#include <stdint.h>

typedef uint64_t ldt_entry_t;
void arch_ldt_invalidate(void);
int arch_ldt_set_entry(unsigned int which, ldt_entry_t entry);
int arch_ldt_fork(ldt_entry_t **dst, ldt_entry_t *src);

#endif
