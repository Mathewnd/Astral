#ifndef _ARCH_BACKTRACE_H
#define _ARCH_BACKTRACE_H

#include <stdbool.h>
#include <arch/context.h>

typedef bool (*backtrace_fn_t)(void *ctx, void *ip);

void arch_get_backtrace(context_t *cpu_context, backtrace_fn_t fn, void *context);

#endif
