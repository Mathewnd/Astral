#ifndef _ARCH_SIGNAL_H
#define _ARCH_SIGNAL_H

#include <kernel/signal.h>

#define ARCH_SIGNAL_STACK_GROWS_DOWNWARDS 1
#define ARCH_SIGNAL_REDZONE_SIZE 128
#define ARCH_SIGNAL_SYSCALL_INSTRUCTION_SIZE 2

#define ARCH_SIGNAL_GETFROMRETURN(x) 

typedef struct {
	void *restorer;
	stack_t oldstack;
	sigset_t oldmask;
	context_t context;
	extracontext_t extracontext;
	siginfo_t siginfo;
} sigframe_t;

#endif
