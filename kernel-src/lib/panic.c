#include <panic.h>
#include <arch/cpu.h>
#include <printf.h>
#include <kernel/interrupt.h>

__attribute__((noreturn)) void _panic(char *msg, context_t *ctx) {
	printf("cpu%lu: Oops.\n", _cpu()->id);

	if (msg)
		printf("%s\n", msg);

	if (ctx)
		PRINT_CTX(ctx);

	interrupt_set(false);
	for (;;);
}
