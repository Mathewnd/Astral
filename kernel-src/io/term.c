#include <flanterm.h>
#include <backends/fb.h>
#include <limine.h>
#include <stddef.h>
#include <stdint.h>
#include <printf.h>
#include <logging.h>
#include <string.h>
#include <kernel/pmm.h>
#include <arch/mmu.h>

extern volatile struct limine_framebuffer_request fb_liminereq;
static size_t xs, ys, fbxs, fbys;

// TODO make more efficient. works for now
static void *internalalloc(size_t n) {
	void *addr = pmm_alloc(n / PAGE_SIZE + 1, PMM_SECTION_DEFAULT);
	__assert(addr);
	return MAKE_HHDM(addr);
}

static struct flanterm_context *term_ctx;
static mutex_t term_mutex;

void term_write(char *str, size_t count) {
	MUTEX_ACQUIRE(&term_mutex, false);
	flanterm_write(term_ctx, str, count);
	MUTEX_RELEASE(&term_mutex);
}

void term_putchar(char c) {
	flanterm_write(term_ctx, &c, 1);
}

void term_getsize(size_t *x, size_t *y, size_t *fbx, size_t *fby) {
	*x = xs;
	*y = ys;
	*fbx = fbxs;
	*fby = fbys;
}

void term_init() {
	__assert(fb_liminereq.response);
	__assert(fb_liminereq.response->framebuffer_count);
	struct limine_framebuffer *fb = fb_liminereq.response->framebuffers[0];

	uint32_t defaultbg = 0x1b1c1b;
	uint32_t defaultfg = 0xffffff;

	// TODO add support for a background if desired by the user
	term_ctx = flanterm_fb_init(internalalloc, fb->address, fb->width, fb->height, fb->pitch,
		NULL, NULL, NULL, &defaultbg, &defaultfg, NULL, NULL, NULL,
		0, 0, 1, 1, 1, 0);

	__assert(term_ctx);

	xs = term_ctx->cols;
	ys = term_ctx->rows;
	fbxs = fb->width;
	fbys = fb->height;

	MUTEX_INIT(&term_mutex);
}
