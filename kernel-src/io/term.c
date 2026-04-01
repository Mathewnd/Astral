#include <flanterm.h>
#include <flanterm_backends/fb.h>
#include <limine.h>
#include <stddef.h>
#include <stdint.h>
#include <printf.h>
#include <logging.h>
#include <string.h>
#include <kernel/pmm.h>
#include <arch/mmu.h>
#include <kernel/init.h>
#include <logging.h>

static volatile struct limine_flanterm_fb_init_params_request flanterm_request = {
	.id = LIMINE_FLANTERM_FB_INIT_PARAMS_REQUEST_ID
};

extern volatile struct limine_framebuffer_request fb_liminereq;
static size_t xs, ys, fbxs, fbys;
bool init_done;

#ifdef TERM_EARLY_INIT
static uint8_t bump[87300000]; // same defaults as flanterm's bump allocator
static size_t bump_off;
static void *internalalloc(size_t n) {
	void *p = &bump[bump_off];
	bump_off += n;
	__assert(bump_off <= 87300000);
	return p;
}
#else
// TODO make more efficient. works for now
static void *internalalloc(size_t n) {
	void *addr = pmm_alloc(n / PAGE_SIZE + 1, PMM_SECTION_DEFAULT);
	__assert(addr);
	return MAKE_HHDM(addr);
}
#endif

static struct flanterm_context *term_ctx;
static mutex_t term_mutex;

void term_write(char *str, size_t count) {
	MUTEX_ACQUIRE(&term_mutex);
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

static void noop(void *, size_t) {

}

void term_init() {
	if (init_done)
		return;

	init_done = true;

	__assert(fb_liminereq.response);
	__assert(fb_liminereq.response->framebuffer_count);
	struct limine_framebuffer *fb = fb_liminereq.response->framebuffers[0];

	uint32_t defaultbg = 0x1b1c1b;
	uint32_t defaultfg = 0xffffff;

	if (flanterm_request.response && flanterm_request.response->entry_count) {
		struct limine_flanterm_fb_init_params *param = flanterm_request.response->entries[0];
		term_ctx = flanterm_fb_init(internalalloc, noop, fb->address, fb->width, fb->height, fb->pitch,
			fb->red_mask_size, fb->red_mask_shift, fb->green_mask_size, fb->green_mask_shift, fb->blue_mask_size, fb->blue_mask_shift,
			param->canvas, param->ansi_colours, param->ansi_bright_colours,
			&param->default_bg, &param->default_fg, &param->default_bg_bright, &param->default_fg_bright,
			param->font, param->font_width, param->font_height, param->font_spacing, param->font_scale_x, param->font_scale_y,
			param->margin, param->rotation);
	} else {
		term_ctx = flanterm_fb_init(internalalloc, noop, fb->address, fb->width, fb->height, fb->pitch,
			fb->red_mask_size, fb->red_mask_shift, fb->green_mask_size, fb->green_mask_shift, fb->blue_mask_size, fb->blue_mask_shift, 
			NULL, NULL, NULL, &defaultbg, &defaultfg, NULL, NULL, NULL,
			0, 0, 1, 1, 1, 0, 0);
	}

	__assert(term_ctx);

	flanterm_get_dimensions(term_ctx, &xs, &ys);
	fbxs = fb->width;
	fbys = fb->height;

	MUTEX_INIT(&term_mutex);
	logging_sethook(term_putchar);
}

INIT_ROUTINE_DEFINE(term, INIT_ROUTINE_FLAGS_NONE, term_init, vmm);
