#include <flanterm.h>
#include <backends/fb.h>
#include <limine.h>
#include <stddef.h>
#include <stdint.h>
#include <printf.h>
#include <logging.h>
#include <string.h>

#define EARLY_BUFFER_SIZE (64*1024*1024)

extern volatile struct limine_framebuffer_request fb_liminereq;

static size_t bufferused;
static uint8_t allocbuffer[EARLY_BUFFER_SIZE];

// TODO once the pmm is done, switch to allocating from it instead.
// right now the terminal is supposed to be initialized before true memory management is initialized.
// thus, there is a simple internal watermark allocator that allocates processor aligned chunks
// of a buffer of size EARLY_BUFFER_SIZE
static void *internalalloc(size_t n) {
	__assert(bufferused + n <= EARLY_BUFFER_SIZE)
	void *ret = &allocbuffer[bufferused];
	bufferused += n + (sizeof(uintmax_t) - (n % sizeof(uintmax_t)));
	return ret;
}

static struct flanterm_context *term_ctx;

void term_write(char *str, size_t count) {
	flanterm_write(term_ctx, str, count);
}

void term_putchar(char c) {
	flanterm_write(term_ctx, &c, 1);
}

void term_init() {
	__assert(fb_liminereq.response);
	__assert(fb_liminereq.response->framebuffer_count);
	struct limine_framebuffer *fb = fb_liminereq.response->framebuffers[0];

	// TODO add support for a background if desired by the user
	term_ctx = flanterm_fb_init(internalalloc, fb->address, fb->width, fb->height, fb->pitch,
		NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
		0, 0, 1, 1, 1, 0);

	__assert(term_ctx);
}
