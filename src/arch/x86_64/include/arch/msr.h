#ifndef _MSR_H_INCLUDE
#define _MSR_H_INCLUDE

#include <stdint.h>

#define MSR_EFER 0xC0000080
#define MSR_STAR 0xC0000081
#define MSR_LSTAR 0xC0000082
#define MSR_CSTAR 0xC0000083
#define MSR_FMASK 0xC0000084
#define MSR_FSBASE 0xC0000100
#define MSR_GSBASE 0xC0000101
#define MSR_KERNELGSBASE 0xC0000102

static inline uint64_t rdmsr(uint32_t which){
	uint64_t low,high;
	asm volatile("rdmsr" : "=a"(low), "=d"(high) : "c"(which));
	return (high << 32) | low;

}

static inline void wrmsr(uint32_t which, uint64_t value){
	uint32_t low = value & 0xFFFFFFFF;
	uint32_t high = (value >> 32) & 0xFFFFFFFF;
		asm volatile("wrmsr" : : "a"(low), "d"(high), "c"(which));
}

#endif
