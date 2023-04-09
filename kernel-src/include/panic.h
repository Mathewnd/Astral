#ifndef _PANIC_H
#define _PANIC_H

#include <arch/context.h>

__attribute__((noreturn)) void _panic(char *msg, context_t *ctx);

#endif
