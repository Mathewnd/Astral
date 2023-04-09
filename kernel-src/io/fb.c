#include <limine.h>

volatile struct limine_framebuffer_request fb_liminereq = {
	.id = LIMINE_FRAMEBUFFER_REQUEST,
	.revision = 0
};
