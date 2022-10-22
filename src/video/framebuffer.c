#include <kernel/devman.h>
#include <limine.h>
#include <string.h>
#include <errno.h>
#include <arch/mmu.h>
#include <arch/cls.h>

static volatile struct limine_framebuffer_request fbreq = {
	.id = LIMINE_FRAMEBUFFER_REQUEST,
	.revision = 0
};

static struct limine_framebuffer** fbs;
static size_t fbcount;

static int devchecks(int fb, size_t* count, size_t offset){
	
	if(fb >= fbcount)
		return ENODEV;

	size_t end = fbs[fb]->pitch * fbs[fb]->height;

	if(offset >= end){
		*count = 0;
		return 0;
	}

	if(*count + offset > end)
		*count = end - offset;

	return 0;
	

}

int read(int* error, int minor, void* buff, size_t count, size_t offset){

	*error = devchecks(minor, &count, offset);

	if(*error)
		return -1;

	memcpy(buff, fbs[minor]->address + offset, count);

	return count;
	
}

int write(int* error, int minor, void* buff, size_t count, size_t offset){
	
	*error = devchecks(minor, &count, offset);

	if(*error)
		return -1;

	memcpy(fbs[minor]->address + offset, buff, count);

	return count;
}

int isatty(int minor){
	return ENOTTY;
}

static int isseekable(int minor, size_t* max){
        
	if(minor >= fbcount)
		return ENODEV;

	*max = fbs[minor]->pitch * fbs[minor]->height;

        return 0;
}

#define FBIOGET_VSCREENINFO	0x4600
#define FBIOPUT_VSCREENINFO	0x4601
#define FBIOGET_FSCREENINFO	0x4602

struct fb_bitfield {
    uint32_t offset;
    uint32_t length;
    uint32_t msb_right;
};

typedef struct{
    uint32_t xres;
    uint32_t yres;
    uint32_t xres_virtual;
    uint32_t yres_virtual;
    uint32_t xoffset;
    uint32_t yoffset;
    uint32_t bits_per_pixel;
    uint32_t grayscale;
    struct fb_bitfield red;
    struct fb_bitfield green;
    struct fb_bitfield blue;
    struct fb_bitfield transp;
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
}fb_var_screeninfo;

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
} fb_fix_screeninfo;

#define FB_VISUAL_TRUECOLOR 2
#define FB_TYPE_PACKED_PIXELS 0


static int ioctl(int minor, unsigned long req, void* arg){

        if(minor >= fbcount)
                return ENODEV;	

	struct limine_framebuffer* fb = fbs[minor];
        switch(req){
		case FBIOPUT_VSCREENINFO: // TODO maybe this should be done properly
			break;
               	case FBIOGET_FSCREENINFO:
			{
			fb_fix_screeninfo* i = arg;
			strcpy(i->id, "LIMINE FB");
			i->smem_start = fb->address;
			i->smem_len = fb->pitch*fb->height;
			i->type = FB_TYPE_PACKED_PIXELS;
			i->visual = FB_VISUAL_TRUECOLOR;
			i->xpanstep = 0;
			i->ypanstep = 0;
			i->line_length = fb->pitch;
			i->mmio_start = 0;
			i->mmio_len = 0;
			i->accel = 0;
			i->capabilities = 0;
			}
			break;
		case FBIOGET_VSCREENINFO:
			{
			fb_var_screeninfo* i = arg;
			i->xres = fb->width;
			i->yres = fb->height;
			i->xres_virtual = fb->width;
			i->yres_virtual = fb->height;
			i->xoffset = 0;
			i->yoffset = 0;
			i->bits_per_pixel = fb->bpp;
			i->grayscale = 0;
			i->red.msb_right = 1;
			i->green.msb_right = 1;
			i->blue.msb_right = 1;
			i->transp.msb_right = 1;
			i->red.offset = fb->red_mask_shift;
			i->red.length = fb->red_mask_size;
			i->blue.offset = fb->blue_mask_shift;
			i->blue.length = fb->blue_mask_size;
			i->green.offset = fb->green_mask_shift;
			i->green.length = fb->green_mask_size;
			i->transp.offset = 28;
			i->transp.length = 8;
			i->nonstd = 0;
			i->activate = 0;
			i->height = 0;
			i->width = 0;
			i->accel_flags = 0;
			i->pixclock = 0;
			i->left_margin = 0;
			i->upper_margin = 0;
			i->lower_margin = 0;
			i->right_margin = 0;
			i->hsync_len = 0;
			i->vsync_len = 0;
			i->vmode = 0;
			i->rotate = 0;
			i->colorspace = 0;
			}
			break;
			default:
                        	return ENOTTY;
        }

        return 0;

}

static map(int minor, void* addr, size_t len, size_t offset, size_t mmuflags){
	
	int properlen = len*PAGE_SIZE;

	int err = devchecks(minor, &properlen, offset);

	properlen /= PAGE_SIZE;

	if(properlen % PAGE_SIZE)
		++properlen;

	if(err)
		return err;

	for(uintmax_t page = 0; page < len; ++page){
		if(page < properlen)
			arch_mmu_map(arch_getcls()->context->context, fbs[minor]->address + offset, addr, mmuflags);

	}

	return 0;
}

static devcalls calls = {
	read, write, isatty, isseekable, ioctl, NULL, map
};


void fb_init(){
	
	if(!fbreq.response)
		return;
	
	fbcount = fbreq.response->framebuffer_count;
	fbs = fbreq.response->framebuffers;
	
	char name[] = { 'f', 'b', '0', 0};

	for(uintmax_t fb = 0; fb < fbcount; ++fb){
		
		devman_newdevice(name, TYPE_CHARDEV, MAJOR_FB, fb, &calls);

		++name[2]; // increase the fbX num

	}

}
