#include <kernel/elf.h>
#include <kernel/alloc.h>
#include <kernel/vmm.h>
#include <errno.h>
#include <string.h>
#include <arch/cls.h>
#include <arch/elfmagic.h>

static size_t phflagtommuflag(size_t phflags){
	
	size_t mmuflags = 0;
	
	if((phflags & ELF_FLAG_EXECUTABLE) == 0)
		mmuflags |= ARCH_MMU_MAP_NOEXEC;
	
	if(phflags & ELF_FLAG_WRITABLE)
		mmuflags |= ARCH_MMU_MAP_WRITE;
	
	if(phflags & ELF_FLAG_READABLE)
		mmuflags |= ARCH_MMU_MAP_READ;

	return mmuflags | ARCH_MMU_MAP_USER;

}

static bool check_header64(elf_header64* header){
	
	if(header->magic != ELF_MAGIC)
		return false;
	
	if(header->bits != ARCH_ELF_BITS)
		return false;
	
	if(header->endianness != ARCH_ELF_ENDIANNESS)
		return false;

	if(header->isa != ARCH_ELF_ISA)
		return false;
	
	return true;

}

static int load(vnode_t* node, elf_ph64 ph){

	size_t mmuflags = phflagtommuflag(ph.flags);

	// allocate memory for the section

	if(!vmm_allocnowat((void*)ph.memaddr, mmuflags, ph.msize))
		return ENOMEM;
	
	// set up temporary mmu flags to copy data

	size_t pagesize = ph.msize / PAGE_SIZE + (ph.msize % PAGE_SIZE ? 1 : 0);

	arch_mmu_changeflags(arch_getcls()->context->context, (void*)ph.memaddr, ARCH_MMU_MAP_READ | ARCH_MMU_MAP_WRITE, pagesize);
	
	int err = 0;

	size_t readc = vfs_read(&err, node, (void*)ph.memaddr, ph.fsize, ph.offset);
	
	if(err)
		return err;
	
	if(readc != ph.fsize)
		return EINVAL;
	
			// zero if needed

	size_t zerocount = ph.msize - ph.fsize;

	if(zerocount){
		void* zeroaddr = (void*)ph.memaddr + ph.fsize;
		memset(zeroaddr, 0, zerocount);
	}

	// reset proper mmu flags

	arch_mmu_changeflags(arch_getcls()->context->context, (void*)ph.memaddr, mmuflags, pagesize);

	return 0;

}

static void* stacksetup(char** argv, char** env, auxv64_list auxv){
	size_t argc = 0;
	size_t envc = 0;
	size_t initialsize = 0;
	size_t argdatasize = 0;
	size_t envdatasize = 0;

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
	
	// pointers to args and envs

	initialsize += argc*sizeof(char*); //args
	initialsize += envc*sizeof(char*); //envs
	initialsize += sizeof(size_t);   //argc	
	
	// allocate memory for the initial section of the stack
	
	// XXX demand alloc the non initialised stack pages
	if(!vmm_allocnowat(STACK_TOP - initialsize - PAGE_SIZE*200, ARCH_MMU_MAP_READ | ARCH_MMU_MAP_WRITE | ARCH_MMU_MAP_NOEXEC | ARCH_MMU_MAP_USER, initialsize + PAGE_SIZE*200)) return NULL;

	// get the addresses to copy the data to

	char* argdatastart = STACK_TOP  - argdatasize;
	char* envdatastart = argdatastart - envdatasize;

	// if the final stack address wouldn't be 16 byte aligned (odd stack entry count), align it

	int alignment = ((argc+1) + (envc+1) + 1) & 1 ? 8 : 0;

	auxv64_list* auxvstart = (void*)(((uintptr_t)envdatastart & (~0xF)) - sizeof(auxv)) + alignment;
	
	// count + 1 because of a null entry
	char** envstart = (void*)auxvstart - (envc + 1)*sizeof(char*);
	char** argstart = (void*)envstart - (argc + 1)*sizeof(char*);

	size_t* argcptr = (void*)argstart - sizeof(size_t);
	
	// copy the data
	
	memcpy(auxvstart, &auxv, sizeof(auxv));


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

	*argcptr = argc;

	

	return argcptr;
	
}

int elf_load(thread_t* thread, vnode_t* node, char** argv, char** env, void** stack, void** entry){
	
	elf_header64 header;
	elf_ph64 phbuff;
	
	int err = 0;
	int readc = vfs_read(&err, node, &header, sizeof(elf_header64), 0);

	if(err)
		return err;

	if(readc != sizeof(elf_header64) || (!check_header64(&header)))
		return EINVAL;
	
	auxv64_list auxv;

	auxv.null.a_type = AT_NULL;
	auxv.phdr.a_type = AT_PHDR;
	auxv.phnum.a_type = AT_PHNUM;
	auxv.phent.a_type = AT_PHENT;
	auxv.entry.a_type = AT_ENTRY;
	
	auxv.phnum.a_val = header.ph_count;
	auxv.phent.a_val = header.ph_size;
	auxv.entry.a_val = header.entry;

	char* interp = NULL; // interpreter name location in program image
	
	for(size_t pos = header.ph_pos, ph = 0; ph < header.ph_count; ++ph, pos += header.ph_size){
		
		readc = vfs_read(&err, node, &phbuff, header.ph_size, pos);
		
		if(err)
			return err;

		if(readc != header.ph_size)
			return EINVAL;

		switch(phbuff.type){
			case PH_TYPE_INTERPRETER:
				interp = (char*)phbuff.memaddr;
				break;
			case PH_TYPE_PHDR:
				auxv.phdr.a_val = phbuff.memaddr;
				break;
			case PH_TYPE_LOAD:
				err = load(node, phbuff);
				if(err)
					return err;
				break;
		}



	}
	
	// load the interpreter if needed
		
	vnode_t* interfile;

	if(interp){

		err = vfs_open(&interfile, thread->proc->root, interp);

		if(err)
			return err;
		
		readc = vfs_read(&err, interfile, &header, sizeof(elf_header64), 0);
		
		if(err)
			goto _interpfail;
	
		if(readc != sizeof(elf_header64)){
			err = EINVAL;
			goto _interpfail;
		}
		
		for(size_t pos = header.ph_pos, ph = 0; ph < header.ph_count; ++ph, pos += header.ph_size){

			readc = vfs_read(&err, interfile, &phbuff, header.ph_size, pos);
			
			if(err)
				goto _interpfail;

			if(readc != header.ph_size){
				err = EINVAL;
				goto _interpfail;
			}
			
			if(phbuff.type != PH_TYPE_LOAD)
				continue;

			err = load(interfile, phbuff);

			if(err)
				goto _interpfail;
			
		}
		
		vfs_close(interfile);

	}

	*stack = stacksetup(argv, env, auxv);
	
	if(!(*stack))
		return ENOMEM;

	*entry = (void*)header.entry;
	return 0;
	
	_interpfail:
		vfs_close(interfile);
		return err;


}
