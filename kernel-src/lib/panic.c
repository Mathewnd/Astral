#include <panic.h>
#include <arch/cpu.h>
#include <arch/smp.h>
#include <printf.h>
#include <kernel/interrupt.h>

__attribute__((noreturn)) void _panic(char *msg, context_t *ctx) {
	interrupt_set(false);
	arch_smp_haltallothers();
	printf("cpu%lu: Oops.\n", current_cpu_id());

	if (msg)
		printf("%s\n", msg);

	if (ctx)
		PRINT_CTX(ctx);

	for (;;);
}
