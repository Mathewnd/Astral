#include <stdio.h>
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"
#include <fcntl.h>
#include <errno.h>
#include <linux/ioctl.h>
#include <linux/fb.h>
#include <sys/mman.h>

void usage(){
	printf("Usage: imgview filename\n");
	exit(0);
}

int main(int argc, char* argv[]){
	if(argc != 2)
		usage();
	
	int fbfd = open("/dev/fb0", O_RDWR);

	if(fbfd == -1){
		
		int err = errno;

		if(err == ENOENT){
			fprintf(stderr, "/dev/fb0 was not found. Make sure there are framebuffers available!\n");
		}
		
		fprintf(stderr, "imgview: failed to open /dev/fb0: %s", strerror(err));

		return -1;

	}
	
	struct fb_fix_screeninfo fbfs;
	struct fb_var_screeninfo fbvs;

	if(ioctl(fbfd, FBIOGET_VSCREENINFO, &fbvs) == -1 || ioctl(fbfd, FBIOGET_FSCREENINFO, &fbfs) == -1){
		perror("imgview: ioctl on framebuffer failed");
		return -1;
	}
	
	if(fbvs.bits_per_pixel != 24 && fbvs.bits_per_pixel != 32){
		fprintf(stderr, "imgview: unsupported bpp!\n");
		return -1;
	}

	size_t bypp = fbvs.bits_per_pixel / 8;

	size_t memsize = fbfs.line_length * fbvs.yres;
	
	void* fbmap = mmap(NULL, memsize, PROT_WRITE | PROT_READ, MAP_SHARED, fbfd, 0);
	if(fbmap == MAP_FAILED){
		perror("imgview: mmap failed on framebuffer");
		return -1;
	}

	int width, height, bytesperpixel;

	char* data = stbi_load(argv[1], &width, &height, &bytesperpixel, 3);
	
	if(data == NULL){
		fprintf(stderr, "imgview: failed to load %s\n", argv[1]);
		return -1;
	}

	for(int y = 0; y < height; ++y){
		for(int x = 0; x < width; ++x){
			char* currentdata = data + x*bytesperpixel + y * width * bytesperpixel;
			void* fbcurr = fbmap + x * bypp + y * fbfs.line_length;
			
			char d[3];

			d[0] = currentdata[2];
			d[1] = currentdata[1];
			d[2] = currentdata[0];

			memcpy(fbcurr, d, 3);
		}
	}


}
