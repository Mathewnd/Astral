#ifndef _IO_H_INCLUDE
#define _IO_H_INCLUDE

static inline void outd(uint16_t port, uint32_t data){
	asm volatile("out %%eax, %%dx" : : "a"(data), "d"(port));
}

static inline uint32_t ind(uint16_t port){
	uint32_t ret;
	asm volatile("in %%dx, %%eax" : "=a"(ret) : "d"(port));
	return ret;
}

static inline void outb(uint16_t port, uint8_t data){
	asm volatile("out %%al, %%dx" : : "a"(data), "d"(port));
}

static inline uint8_t inb(uint16_t port){
	uint8_t ret;
	asm volatile ("in %%dx, %%al" : "=a"(ret) : "d"(port));
	return ret;
}

#endif
