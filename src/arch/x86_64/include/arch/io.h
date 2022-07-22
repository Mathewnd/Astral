#ifndef _IO_H_INCLUDE
#define _IO_H_INCLUDE

static inline void outb(uint16_t port, uint8_t data){
	asm("out %%al, %%dx" : : "a"(data), "d"(port));
}

static inline uint8_t inb(uint16_t port){
	uint8_t ret;
	asm("in %%dx, %%al" : "=a"(ret) : "d"(port));
	return ret;
}

#endif
