#include <kernel/elf.h>
#include <kernel/alloc.h>
#include <kernel/vmm.h>
#include <errno.h>
#include <string.h>
#include <arch/cls.h>

typedef struct{
	uint64_t a_type;
	uint64_t a_val;
} auxv64_t;

typedef struct {
	auxv64_t phdr;
	auxv64_t null;
} auxv64_list;

static size_t phflagtommuflag(size_t phflags){
	
	size_t mmuflags = 0;
	
	if(phflags & ELF_FLAG_EXECUTABLE == 0)
		mmuflags |= ARCH_MMU_MAP_NOEXEC;
	
	if(phflags & ELF_FLAG_WRITABLE)
		mmuflags |= ARCH_MMU_MAP_WRITE;
	
	if(phflags & ELF_FLAG_READABLE)
		mmuflags |= ARCH_MMU_MAP_READ;

	return mmuflags | ARCH_MMU_MAP_USER;

}

static bool check_header64(elf_header64* header){
	// XXX throw this into arch

	if(header->magic != ELF_MAGIC)
		return false;
	
	if(header->bits != ELF_BITS_64)
		return false;
	
	if(header->endianness != ELF_ENDIANNESS_LITTLE)
		return false;

	if(header->isa != ELF_ISA_X86_64)
		return false;
	
	return true;

}

static void* stacksetup(char* interp, char** argv, char** env, elf_header64 header, elf_ph64* phs){
	size_t argc = 0;
	size_t envc = 0;
	size_t initialsize = 0;
	size_t argdatasize = 0;
	size_t envdatasize = 0;
	size_t auxvsize    = 5*8; // FIXME better way of keeping track of this

	while(argv[argc]){
		argdatasize += strlen(argv[argc]) + 1;
		initialsize += strlen(argv[argc]) + 1;
		++argc;
	}

	while(env[envc]){
		envdatasize += strlen(env[envc]) + 1;
		initialsize += strlen(env[envc]) + 1;
		++envc;
	}
	
	if(interp){
		argdatasize += strlen(interp) + 1;
		initialsize += strlen(interp) + 1;
		initialsize += sizeof(char*); // interpreter ptr
	}
	
	// pointers to args and envs

	initialsize += argc*sizeof(char*); //args
	initialsize += envc*sizeof(char*); //envs
	initialsize += sizeof(size_t);   //argc	
	
	// allocate memory for the initial section of the stack
	
	// XXX demand alloc the non initialised stack pages
	if(!vmm_allocnowat(STACK_TOP - initialsize - PAGE_SIZE*200, ARCH_MMU_MAP_READ | ARCH_MMU_MAP_WRITE | ARCH_MMU_MAP_NOEXEC | ARCH_MMU_MAP_USER, initialsize + PAGE_SIZE*200)) return NULL;

	// get the addresses to copy the data to

	elf_ph64* phdrdatastart = STACK_TOP - header.ph_count * header.ph_size;
	char* argdatastart = (void*) phdrdatastart - argdatasize;
	char* envdatastart = argdatastart - envdatasize;
	
	auxv64_t* auxvstart = (void*)(((uintptr_t)envdatastart & (~0xF)) - auxvsize);
	// count + 1 because of a null entry
	char** envstart = (void*)auxvstart - (envc + 1)*sizeof(char*);
	char** argstart = (void*)envstart - (argc + 1)*sizeof(char*);
	if(interp)
		--argstart;
	
	size_t* argcptr = (void*)argstart - sizeof(size_t);
	
	// auxv

	// AT_PHDR
	
	auxvstart->a_type = AT_PHDR;
	auxvstart->a_val = (uint64_t)phdrdatastart;
	auxvstart++;
	
	memcpy(phdrdatastart, phs, header.ph_count * header.ph_size);

	// AT_PHENT

	auxvstart->a_type = AT_PHENT;
	auxvstart->a_val = header.ph_size;
	auxvstart++;

	// AT_PHNUM

	auxvstart->a_type = AT_PHNUM;
	auxvstart->a_val = header.ph_count;

	// AT_ENTRY
	
	auxvstart->a_type = AT_ENTRY;
	auxvstart->a_val = header.entry;
	auxvstart++;

	// AT_NULL

	auxvstart->a_type = AT_NULL;

	// copy the data


	if(interp){
		strcpy(argdatastart, interp);
		*argstart++ = argdatastart;
		argdatastart += strlen(interp) + 1;
	}

	for(size_t i = 0; i < argc; ++i){
		strcpy(argdatastart, argv[i]);
		*argstart++ = argdatastart;
		argdatastart += strlen(argv[i]) + 1;
	}

	*argstart = NULL;
	
	for(size_t i = 0; i < envc; ++i){
		strcpy(envdatastart, env[i]);
		*envstart++ = envdatastart;
		envdatastart += strlen(env[i]) + 1;
	}

	*envstart = NULL;

	if(interp)
		argc++;
	
	*argcptr = argc;

	

	return argcptr;
	
}

int elf_load(thread_t* thread, vnode_t* node, char** argv, char** env){
	
	elf_header64 header;	
	elf_header64 progheader;
	elf_ph64 ph; 

	int err = 0;
	int readc = vfs_read(&err, node, &header, sizeof(elf_header64), 0);
	char* interp = NULL;

	if(err)
		return err;
	

	if(readc != sizeof(elf_header64) || (!check_header64(&header)))
		return EINVAL;
	
	progheader = header; // incase an interpreter exists
	
	// find out if this needs an interpreter
	// and also save the executable's program headers
	// for auxv

	elf_ph64 progphs[header.ph_count];

	for(size_t currentph = 0; currentph < header.ph_count; ++currentph){	
		readc = vfs_read(&err, node, &ph, header.ph_size, header.ph_pos + currentph*header.ph_size);
		if(err)
			return err;
		if(readc != header.ph_size)
			return EINVAL;
		
		memcpy(progphs+currentph, &ph, sizeof(elf_ph64));

		if(ph.type == PH_TYPE_INTERPRETER){

			// load the name and elf header of the interpreter

			progheader = header;

			char* interp = alloc(ph.fsize + 1);
			if(!interp)
				return ENOMEM;

			readc = vfs_read(&err, node, interp, ph.fsize, ph.offset);
			if(err){
				free(interp);
				return err;
			}
			if(ph.fsize != readc){
				free(interp);
				return EINVAL;
			}

			err = vfs_open(&node, thread->proc->root, interp);

			if(err){
				free(interp);
				return err;
			}

			readc = vfs_read(&err, node, &header, sizeof(elf_header64), 0);

			if(err)
				goto _fail;

			if(readc != sizeof(elf_header64) || (!check_header64(&header))){
				err = EINVAL;
				goto _fail;
			}

		}
		
	}

	// load program into memory

	for(size_t currentph = 0; currentph < header.ph_count; ++currentph){
		
		// read ph

		readc = vfs_read(&err, node, &ph, header.ph_size, header.ph_pos + currentph*header.ph_size);
		
		if(err) goto _fail;

		if(readc != header.ph_size){
			err = EINVAL;
			goto _fail;
		}
		
		if(ph.type != PH_TYPE_LOAD)
			continue;

		if(!vmm_allocnowat((void*)ph.memaddr, phflagtommuflag(ph.flags), ph.msize)){
			err = ENOMEM;
			goto _fail;
		}

		// set up temporary mmu flags to copy data
		
		arch_mmu_changeflags(arch_getcls()->context->context, ph.memaddr, ARCH_MMU_MAP_READ | ARCH_MMU_MAP_WRITE, ph.msize / PAGE_SIZE + (ph.msize % PAGE_SIZE ? 1 : 0));

		// read program data
		
		readc = vfs_read(&err, node, (void*)ph.memaddr, ph.fsize, ph.offset);
		
		if(err)
			goto _fail;

		if(readc != ph.fsize){
			err = EINVAL;
			goto _fail;
		}

		// zero if needed
		
		size_t zerocount = ph.msize - ph.fsize;

		if(zerocount){
			void* zeroaddr = ph.memaddr + ph.fsize;
			memset(zeroaddr, 0, zerocount);
		}
		
		// reset proper mmu flags
		
		arch_mmu_changeflags(arch_getcls()->context->context, ph.memaddr, phflagtommuflag(ph.flags), ph.msize / PAGE_SIZE + (ph.msize % PAGE_SIZE ? 1 : 0));


	}
	
	

	

	void* stack = stacksetup(interp, argv, env, progheader, progphs);
	
	arch_regs_setupuser(thread->regs, (void*)header.entry, stack, true);

	return 0;

	_fail:
		if(interp){
			free(interp);
			vfs_close(node);
		}
	
	return err;

}
