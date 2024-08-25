#include <arch/io.h>
#include <kernel/devfs.h>
#include <logging.h>

void arch_e9_putc(char c) {
#ifdef X86_64_ENABLE_E9
	outb(0xe9, c);
#endif
}

void arch_e9_puts(char *c) {
#ifdef X86_64_ENABLE_E9
	while (*c) {
		outb(0xe9, *c++);
	}
#endif
}

static int devwrite(int minor, void *bufferp, size_t count, uintmax_t offset, int flags, size_t *wcount) {
	uint8_t *buffer = bufferp;

	for (uintmax_t i = 0; i < count; ++i)
		outb(0xe9, buffer[i]);

	*wcount = count;
	return 0;
}

static devops_t devops = {
	.write = devwrite
};

void arch_e9_initdev() {
#ifdef X86_64_ENABLE_E9
	__assert(devfs_register(&devops, "e9", V_TYPE_CHDEV, DEV_MAJOR_E9, 0, 0666, NULL) == 0);
#endif
}
