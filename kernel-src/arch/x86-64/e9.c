#include <arch/io.h>
#include <kernel/devfs.h>
#include <logging.h>
#include <kernel/usercopy.h>

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

#ifdef X86_64_ENABLE_E9
static int devwrite(int minor, iovec_iterator_t *iovec_iterator, size_t count, uintmax_t offset, int flags, size_t *wcount) {
	for (uintmax_t i = 0; i < count; ++i) {
		char c;
		int error = iovec_iterator_copy_to_buffer(iovec_iterator, &c, 1);
		if (error)
			return error;

		outb(0xe9, c);
	}

	*wcount = count;
	return 0;
}

static devops_t devops = {
	.write = devwrite
};
#endif

void arch_e9_initdev() {
#ifdef X86_64_ENABLE_E9
	__assert(devfs_register(&devops, "e9", V_TYPE_CHDEV, DEV_MAJOR_E9, 0, 0666, NULL) == 0);
#endif
}
