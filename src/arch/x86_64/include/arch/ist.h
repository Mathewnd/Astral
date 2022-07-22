#ifndef _IST_H_INCLUDE
#define _IST_H_INCLUDE

#include <stdint.h>

typedef struct{
	uint32_t IOPB;
	uint64_t reserved3;
	uint64_t ist7;
	uint64_t ist6;
	uint64_t ist5;
	uint64_t ist4;
	uint64_t ist3;
	uint64_t ist2;
	uint64_t ist1;
	uint64_t reserved2;
	uint64_t rsp2;
	uint64_t rsp1;
	uint64_t rsp0;
	uint64_t reserved;
} __attribute((packed)) ist_t;

#endif
