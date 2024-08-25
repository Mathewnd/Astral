#include <limine.h>
#include <stdint.h>
#include <stddef.h>
#include <logging.h>
#include <kernel/alloc.h>
#include <kernel/devfs.h>
#include <arch/cpu.h>
#include <kernel/pmm.h>
#include <kernel/usercopy.h>

typedef struct bitfield_t {
	uint32_t offset;
	uint32_t length;
	uint32_t msb_right;
} bitfield_t;

typedef struct {
	uint32_t xres;
	uint32_t yres;
	uint32_t xres_virtual;
	uint32_t yres_virtual;
	uint32_t xoffset;
	uint32_t yoffset;
	uint32_t bits_per_pixel;
	uint32_t grayscale;
	bitfield_t red;
	bitfield_t green;
	bitfield_t blue;
	bitfield_t transp;
	uint32_t nonstd;
	uint32_t activate;
	uint32_t height;
	uint32_t width;
	uint32_t accel_flags;
	uint32_t pixclock;
	uint32_t left_margin;
	uint32_t right_margin;
	uint32_t upper_margin;
	uint32_t lower_margin;
	uint32_t hsync_len;
	uint32_t vsync_len;
	uint32_t sync;
	uint32_t vmode;
	uint32_t rotate;
	uint32_t colorspace;
	uint32_t reserved[4];
} varinfo_t;

typedef struct {
	char id[16];
	unsigned long smem_start;
	uint32_t smem_len;
	uint32_t type;
	uint32_t type_aux;
	uint32_t visual;
	uint16_t xpanstep;
	uint16_t ypanstep;
	uint16_t ywrapstep;
	uint32_t line_length;
	unsigned long mmio_start;
	uint32_t mmio_len;
	uint32_t accel;
	uint16_t capabilities;
	uint16_t reserved[2];
} fixinfo_t;

#define FB_VISUAL_TRUECOLOR 2
#define FB_TYPE_PACKED_PIXELS 0

volatile struct limine_framebuffer_request fb_liminereq = {
	.id = LIMINE_FRAMEBUFFER_REQUEST,
	.revision = 0
};

static size_t count;
static struct limine_framebuffer **fbs;
static fixinfo_t *fixinfos;
static varinfo_t *varinfos;

static int maxseek(int minor, size_t *max) {
	if (minor >= count)
		return ENODEV;

	*max = fbs[minor]->pitch * fbs[minor]->height;

	return 0;
}

static int read(int minor, void *buffer, size_t size, uintmax_t offset, int flags, size_t *readc) {
	if (minor >= count)
		return ENODEV;

	uintmax_t end = fbs[minor]->pitch * fbs[minor]->height;
	*readc = 0;

	if (offset >= end)
		return 0;

	if(size + offset > end)
		size = end - offset;

	memcpy(buffer, (void *)((uintptr_t)fbs[minor]->address + offset), size);
	*readc = size;

	return 0;
}

static int write(int minor, void *buffer, size_t size, uintmax_t offset, int flags, size_t *writec) {
	if (minor >= count)
		return ENODEV;

	uintmax_t end = fbs[minor]->pitch * fbs[minor]->height;
	*writec = 0;

	if (offset >= end)
		return 0;

	if(size + offset > end)
		size = end - offset;

	memcpy((void *)((uintptr_t)fbs[minor]->address + offset), buffer, size);
	*writec = size;

	return 0;
}

static int mmap(int minor, void *addr, uintmax_t offset, int flags) {
	if (minor >= count)
		return ENODEV;

	uintmax_t end = fbs[minor]->pitch * fbs[minor]->height;
	__assert(offset < end);

	void *paddr = NULL;

	if (flags & V_FFLAGS_SHARED) {
		// make sure offset is ALWAYS page aligned when doing a shared mapping
		__assert((offset % PAGE_SIZE) == 0);
		return arch_mmu_map(_cpu()->vmmctx->pagetable, FROM_HHDM((void *)((uintptr_t)fbs[minor]->address + offset)), addr, vnodeflagstommuflags(flags)) ? 0 : ENOMEM;
	} else {
		size_t size = offset + PAGE_SIZE < end ? PAGE_SIZE : end - offset;
		paddr = pmm_allocpage(PMM_SECTION_DEFAULT);
		if (paddr == NULL)
			return ENOMEM;

		memcpy(MAKE_HHDM(paddr), (void *)((uintptr_t)fbs[minor]->address + offset), size);

		if (arch_mmu_map(_cpu()->vmmctx->pagetable, paddr, addr, vnodeflagstommuflags(flags)) == false) {
			pmm_release(paddr);
			return ENOMEM;
		}

		return 0;
	}
}

static int munmap(int minor, void *addr, uintmax_t offset, int flags) {
	void *phys = arch_mmu_getphysical(_cpu()->vmmctx->pagetable, addr);
	arch_mmu_unmap(_cpu()->vmmctx->pagetable, addr);
	if ((flags & V_FFLAGS_SHARED) == 0) {
		pmm_release(phys);
	}

	return 0;
}

#define FBIOGET_VSCREENINFO	0x4600
#define FBIOPUT_VSCREENINFO	0x4601
#define FBIOGET_FSCREENINFO	0x4602
#define FBIOBLANK		0x4611

static int ioctl(int minor, unsigned long request, void *arg, int *result){
        if (minor >= count)
                return ENODEV;

        switch (request) {
		case FBIOBLANK:
		case FBIOPUT_VSCREENINFO:
			break;
		case FBIOGET_FSCREENINFO:
			return USERCOPY_POSSIBLY_TO_USER(arg, &fixinfos[minor], sizeof(fixinfo_t));
		case FBIOGET_VSCREENINFO:
			return USERCOPY_POSSIBLY_TO_USER(arg, &varinfos[minor], sizeof(varinfo_t));
		default:
			return ENOTTY;
        }

        return 0;
}

static devops_t ops = {
	.read = read,
	.write = write,
	.mmap = mmap,
	.munmap = munmap,
	.ioctl = ioctl,
	.maxseek = maxseek
};

void fb_init() {
	if (fb_liminereq.response == NULL || fb_liminereq.response->framebuffer_count == 0) {
		printf("fb: no framebuffer devices\n");
		return;
	}

	fbs = fb_liminereq.response->framebuffers;
	count = fb_liminereq.response->framebuffer_count;
	printf("fb: %d framebuffer device%c\n", count, count > 1 ? 's' : ' ');

	fixinfos = alloc(count * sizeof(fixinfo_t));
	varinfos = alloc(count * sizeof(varinfo_t));
	__assert(fixinfos && varinfos);

	for (int i = 0; i < count; ++i) {
		struct limine_framebuffer* fb = fbs[i];

		varinfo_t* vi = &varinfos[i];
		vi->xres = fb->width;
		vi->yres = fb->height;
		vi->xres_virtual = fb->width;
		vi->yres_virtual = fb->height;
		vi->bits_per_pixel = fb->bpp;
		vi->red.msb_right = 1;
		vi->green.msb_right = 1;
		vi->blue.msb_right = 1;
		vi->transp.msb_right = 1;
		vi->red.offset = fb->red_mask_shift;
		vi->red.length = fb->red_mask_size;
		vi->blue.offset = fb->blue_mask_shift;
		vi->blue.length = fb->blue_mask_size;
		vi->green.offset = fb->green_mask_shift;
		vi->green.length = fb->green_mask_size;
		vi->height = -1;
		vi->width = -1;

		fixinfo_t* fi = &fixinfos[i];
		strcpy(fi->id, "LIMINE FB");
		fi->smem_len = fb->pitch * fb->height;
		fi->type = FB_TYPE_PACKED_PIXELS;
		fi->visual = FB_VISUAL_TRUECOLOR;
		fi->line_length = fb->pitch;
		fi->mmio_len = fb->pitch * fb->height;

		char name[6];
		snprintf(name, 6, "fb%d", i);

		__assert(devfs_register(&ops, name, V_TYPE_CHDEV, DEV_MAJOR_FB, i, 0666, NULL) == 0);

		printf("%s: %dx%d %d bpp\n", name, fb->width, fb->height, fb->bpp);
	}
}
