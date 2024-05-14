#include <kernel/syscalls.h>
#include <arch/signal.h>
#include <arch/cpu.h>
#include <logging.h>

// this expects CTX_SP to be pointing to the signal frame
__attribute__((noreturn)) void syscall_sigreturn(context_t *context) {
	// TODO check if the information is valid
	sigframe_t sigframe;
	memcpy(&sigframe, (void *)CTX_SP(context), sizeof(sigframe_t));

	interrupt_set(false);
	signal_altstack(_cpu()->thread, &sigframe.oldstack, NULL);
	signal_changemask(_cpu()->thread, SIG_SETMASK, &sigframe.oldmask, NULL);

	__assert(ARCH_CONTEXT_ISUSER(&sigframe.context));
	memcpy(&_cpu()->thread->extracontext, &sigframe.extracontext, sizeof(extracontext_t));
	ARCH_CONTEXT_THREADLOAD(_cpu()->thread, context);

	arch_context_switch(&sigframe.context);

	// if we were to return normally, CTX_RET and CTX_ERRNO would be corrupted,
	// so we will manually call arch_context_switch, which will jump to the right userland code
	// (and also check for pending signals again)
	__builtin_unreachable();
}
