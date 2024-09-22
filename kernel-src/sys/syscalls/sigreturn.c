#include <kernel/syscalls.h>
#include <arch/signal.h>
#include <arch/cpu.h>
#include <logging.h>

// this expects CTX_SP to be pointing to the signal frame
__attribute__((noreturn)) void syscall_sigreturn(context_t *context) {
	sigframe_t sigframe;
	int error = usercopy_fromuser(&sigframe, (void *)CTX_SP(context), sizeof(sigframe_t));
	if (error || ARCH_CONTEXT_ISUSER(&sigframe.context) == false) {
		printf("syscall_sigreturn: bad return stack or bad return information\n");
		proc_terminate(SIGSEGV);
	}

	interrupt_set(false);
	signal_altstack(current_thread(), &sigframe.oldstack, NULL);
	signal_changemask(current_thread(), SIG_SETMASK, &sigframe.oldmask, NULL);

	__assert(ARCH_CONTEXT_ISUSER(&sigframe.context));
	memcpy(&current_thread()->extracontext, &sigframe.extracontext, sizeof(extracontext_t));
	ARCH_CONTEXT_THREADLOAD(current_thread(), context);

	interrupt_set(true);
	arch_context_switch(&sigframe.context);

	// if we were to return normally, CTX_RET and CTX_ERRNO would be corrupted,
	// so we will manually call arch_context_switch, which will jump to the right userland code
	// (and also check for pending signals again)
	__builtin_unreachable();
}
