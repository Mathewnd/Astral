#include <panic.h>
#include <arch/cpu.h>
#include <arch/smp.h>
#include <printf.h>
#include <kernel/interrupt.h>
#include <arch/backtrace.h>
#include <dbg.h>

static bool paniced = false;

static bool step_backtrace(void *, void *ip) {
	char *fn, *file;
	size_t off;

	dbg_get_symbol(ip, &fn, &file, &off);

	printf("%p: %s:%s+%x\n", ip, file, fn, off);
	return true;
}

static void print_backtrace(context_t *context, void *) {
	arch_get_backtrace(context, step_backtrace, NULL);
}

__attribute__((noreturn)) void _panic(char *msg, context_t *ctx) {
	interrupt_set(false);
	if (arch_smp_cpusawake > 1)
		arch_smp_haltallothers();

	printf("cpu%lu: Oops.\n", current_cpu_id());

	if (msg)
		printf("%s\n", msg);

	if (ctx)
		PRINT_CTX(ctx);

	if (!paniced) {
		paniced = true;
		printf("backtrace:\n");
		if (ctx == NULL)
			arch_context_saveandcall(print_backtrace, NULL, NULL);
		else
			print_backtrace(ctx, NULL);
	} else {
		printf("!! DOUBLE PANIC !!\n");
	}

	for (;;);
}
