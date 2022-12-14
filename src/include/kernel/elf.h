#ifndef _ELF_H_INCLUDE
#define _ELF_H_INCLUDE

#include <kernel/sched.h>
#include <kernel/vfs.h>
#include <stddef.h>
#include <stdint.h>

#define ELF_MAGIC 0x464c457f // 0x7F ELF

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

#define STACK_TOP (void*)0x800000000000

#define AT_NULL 0
#define AT_PHDR 3
#define AT_PHENT 4
#define AT_PHNUM 5
#define AT_ENTRY 9

typedef struct{
	uint64_t a_type;
	uint64_t a_val;
} auxv64_t;

typedef struct {
	auxv64_t phdr;
	auxv64_t phnum;
	auxv64_t phent;
	auxv64_t entry;
	auxv64_t null;
} auxv64_list;

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
#define PH_TYPE_SHLIB 5
#define PH_TYPE_PHDR 6

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

// loads an elf executable from file
// also sets up the stack for said executable

int elf_load(thread_t* thread, vnode_t* node, char** argv, char** env, void** entry, void** stack);

#endif
