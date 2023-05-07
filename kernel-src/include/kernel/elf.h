#ifndef _ELF_H
#define _ELF_H

#include <kernel/scheduler.h>
#include <kernel/vfs.h>
#include <stddef.h>
#include <stdint.h>

#define ELF_MAGIC 0x464c457f

#define ELF_RELOCATABLE 1
#define ELF_EXECUTABLE 2
#define ELF_SHARED 3
#define ELF_CORE 4

#define ELF_SEGMENT_NULL 0
#define ELF_SEGMENT_LOAD 1
#define ELF_SEGMENT_DYNAMIC 2
#define ELF_SEGMENT_INTERP 3
#define ELF_SEGMENT_NOTE 4

#define ELF_FLAGS_EXECUTABLE 1
#define ELF_FLAGS_WRITABLE 2
#define ELF_FLAGS_READABLE 4

#define AT_NULL 0
#define AT_PHDR 3
#define AT_PHENT 4
#define AT_PHNUM 5
#define AT_ENTRY 9

typedef struct {
	uint64_t type;
	uint64_t val;
} auxv64_t;

typedef struct {
	auxv64_t phdr;
	auxv64_t phnum;
	auxv64_t phent;
	auxv64_t entry;
	auxv64_t null;
} auxv64list_t;

typedef struct {
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
	uint64_t phpos;
	uint64_t shpos;
	uint32_t flags;
	uint16_t headsize;
	uint16_t phsize;
	uint16_t phcount;
	uint16_t shsize;
	uint16_t shcount;
	uint16_t shnames;
} __attribute__((packed)) elfheader64_t;

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
} __attribute__((packed)) elfph64_t;

int elf_load(vnode_t *vnode, void *base, void **entry, char **interpreter, auxv64list_t *auxv64);
void *elf_preparestack(void *top, auxv64list_t *auxv64, char **argv, char **envp);

#endif
