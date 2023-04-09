#include <panic.h>
#include <arch/cpu.h>
#include <printf.h>

__attribute__((noreturn)) void _panic(char *msg, context_t *ctx) {
	printf("Oops.\n");

	if (msg)
		printf("%s\n", msg);

	if (ctx)
		PRINT_CTX(ctx);

	for (;;);
}
