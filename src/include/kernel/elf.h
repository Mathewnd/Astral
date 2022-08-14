#ifndef _ELF_H_INCLUDE
#define _ELF_H_INCLUDE

#include <kernel/sched.h>
#include <kernel/vfs.h>
#include <stddef.h>
#include <stdint.h>

#define ELF_MAGIC 0x464c457f // 0x7F ELF
#define ELF_BITS_64 2
#define ELF_ISA_X86_64 0x3E
#define ELF_ENDIANNESS_LITTLE 1

#define ELF_RELOCATABLE 1
#define ELF_EXECUTABLE 2
#define ELF_SHARED 3
#define ELF_CORE 4

#define ELF_SEGMENT_NULL 0
#define ELF_SEGMENT_LOAD 1
#define ELF_SEGMENT_DYNAMIC 2
#define ELF_SEGMENT_INTERP 3
#define ELF_SEGMENT_NOTE 4

#define ELF_FLAG_EXECUTABLE 1
#define ELF_FLAG_WRITABLE 2
#define ELF_FLAG_READABLE 4


typedef struct{
	uint32_t magic;
	uint8_t bits;
	uint8_t endianness;
	uint8_t headversion;
	uint8_t abi;
	uint64_t padding;
	uint16_t type;
	uint16_t isa;
	uint32_t elfver;
	uint64_t entry;
	uint64_t ph_pos;
	uint64_t sh_pos;
	uint32_t flags;
	uint16_t headsize;
	uint16_t ph_size;
	uint16_t ph_count;
	uint16_t sh_size;
	uint16_t sh_count;
	uint16_t sh_names;
} __attribute__((packed)) elf_header64;

#define PH_TYPE_LOAD 1
#define PH_TYPE_DYNAMIC 2
#define PH_TYPE_INTERPRETER 3
#define PH_TYPE_NOTE 4

typedef struct{
	uint32_t type;
	uint32_t flags;
	uint64_t offset;
	uint64_t memaddr;
	uint64_t undefined;
	uint64_t fsize;
	uint64_t msize;
	uint64_t alignment;
} elf_ph64;

// loads an elf executable from file, setting target thread with appropriate
// values for it
// also sets up the stack for said executable

int elf_load(thread_t* thread, vnode_t* node, char** argv, char** env);

#endif
