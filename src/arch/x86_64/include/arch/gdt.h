#ifndef _GDT_H_INCLUDE
#define _GDT_H_INCLUDE

#define ACCESS_PRESENT (1 << 7)
#define ACCESS_DPL3    (3 << 5)
#define ACCESS_SEGMENT (1 << 4)
#define ACCESS_EXEC    (1 << 3)
#define ACCESS_DIR     (1 << 2)
#define ACCESS_CONFORM (1 << 2)
#define ACCESS_CODE_READ (1 << 1)
#define ACCESS_DATA_WRITE (1 << 1)

#define FLAGS_LONG (1 << 1)
#define FLAGS_PMODE (1 << 2)
#define FLAGS_PAGE_GRANULARITY (1 << 3)

#define SYSTEM_TYPE_IST 0x9

#include <stdint.h>

typedef struct{
	uint16_t size;
	uint64_t offset;
} __attribute__((packed)) gdtr_t;

typedef struct{
	uint16_t limit;
	uint16_t offset1;
	uint8_t	 offset2;
	uint8_t	 access;
	uint8_t	 flags;
	uint8_t	 offset3;
} __attribute__((packed)) segdesc;

typedef struct{
	uint16_t limit;
	uint16_t offset1;
	uint8_t  offset2;
	uint8_t  access;
	uint8_t flags;
	uint8_t offset3;
	uint32_t offset4;
	uint32_t reserved;
} __attribute__((packed)) sys64;

typedef struct{ 
	segdesc null;
	segdesc kcode16;
	segdesc kdata16;
	segdesc kcode32;
	segdesc kdata32;
	segdesc kcode64;
	segdesc kdata64;
	segdesc ucode64;
	segdesc udata64;
	sys64 ist;
} gdt_t;

void gdt_bspinit();

#endif
