#include <kernel/elf.h>
#include <logging.h>
#include <errno.h>
#include <kernel/alloc.h>
#include <util.h>
#include <kernel/vmm.h>

#ifdef	__x86_64__
	#define ARCH_ELF_BITS 2
	#define ARCH_ELF_ISA 0x3e
	#define ARCH_ELF_ENDIANNESS 1
#else
	#error UNSUPPORTED ARCH
#endif

static bool headerok(elfheader64_t *header) {
	if (header->magic != ELF_MAGIC)
		return false;

	if (header->bits != ARCH_ELF_BITS)
		return false;

	if (header->endianness != ARCH_ELF_ENDIANNESS)
		return false;

	if (header->isa != ARCH_ELF_ISA)
		return false;

	return true;
}

static int readexact(vnode_t *vnode, void *buff, size_t count, uintmax_t offset) {
	size_t readc;
	int e = vfs_read(vnode, buff, count, offset, &readc, 0);
	if (e)
		return e;
	if (readc != count)
		return ENOEXEC;
	return 0;
}

static mmuflags_t flagstommuflags(int flags) {
	mmuflags_t mmuflags = ARCH_MMU_FLAGS_USER;
	if (flags & ELF_FLAGS_READABLE)
		mmuflags |= ARCH_MMU_FLAGS_READ;
	if (flags & ELF_FLAGS_WRITABLE)
		mmuflags |= ARCH_MMU_FLAGS_WRITE;
	if ((flags & ELF_FLAGS_READABLE) & ELF_FLAGS_EXECUTABLE)
		mmuflags |= ARCH_MMU_FLAGS_NOEXEC;

	return mmuflags;
}

static int load(vnode_t *vnode, elfph64_t *ph) {
	int error = 0;
	uintmax_t foffset = ph->offset;
	uintptr_t mempos = ph->memaddr;
	size_t msize = ph->msize;
	size_t fsize = ph->fsize;
	mmuflags_t mmuflags = flagstommuflags(ph->flags);

	uintmax_t firstpageoffset = mempos % PAGE_SIZE;
	// start is not page aligned
	if (firstpageoffset) {
		size_t firstpagecount = PAGE_SIZE - firstpageoffset;
		firstpagecount = fsize > firstpagecount ? firstpagecount : fsize;
		void *page = (void *)ROUND_DOWN(mempos, PAGE_SIZE);
		if (vmm_map(page, PAGE_SIZE, VMM_FLAGS_EXACT | VMM_FLAGS_ALLOCATE, mmuflags | ARCH_MMU_FLAGS_WRITE, NULL) == NULL)
			return ENOMEM;

		error = readexact(vnode, (void *)mempos, firstpagecount, foffset);
		if (error)
			return error;

		// TODO switch out protection

		memset(page, 0, firstpageoffset);
		mempos += firstpagecount;
		msize -= firstpagecount;
		fsize -= firstpagecount;
		foffset += firstpagecount;
		uintmax_t remaining = PAGE_SIZE - (mempos % PAGE_SIZE);
		// zero remaining parts of the first page if needed
		if (remaining && msize) {
			size_t remainingmsize = remaining > msize ? msize : remaining;
			size_t pagediff = ROUND_UP(mempos, PAGE_SIZE) - mempos;
			if (remainingmsize > ROUND_UP(mempos, PAGE_SIZE) - mempos)
				remainingmsize = pagediff;

			memset((void *)mempos, 0, remainingmsize);
			msize -= remainingmsize;
			mempos += remainingmsize;
		}
	}

	// map middle of file
	size_t filepagecount = fsize / PAGE_SIZE;
	if (filepagecount) {
		vmmfiledesc_t vfd = {
			.node = vnode,
			.offset = foffset
		};
		if (vmm_map((void *)mempos, filepagecount, VMM_FLAGS_EXACT | VMM_FLAGS_PAGESIZE | VMM_FLAGS_FILE, mmuflags, &vfd) == NULL)
			return ENOMEM;
		size_t bytesize = filepagecount * PAGE_SIZE;
		mempos += bytesize;
		msize -= bytesize;
		fsize -= bytesize;
		foffset += bytesize;
	}

	// map end of file
	size_t lastpagecount = fsize % PAGE_SIZE;
	if (lastpagecount) {
		if (vmm_map((void *)mempos, PAGE_SIZE, VMM_FLAGS_EXACT | VMM_FLAGS_ALLOCATE, mmuflags | ARCH_MMU_FLAGS_WRITE, NULL) == NULL)
			return ENOMEM;

		error = readexact(vnode, (void *)mempos, lastpagecount, foffset);
		if (error)
			return error;

		// TODO switch out protection

		memset((void *)(mempos + lastpagecount), 0, PAGE_SIZE - lastpagecount);
		mempos += PAGE_SIZE;
		msize -= msize > PAGE_SIZE ? PAGE_SIZE : msize;
	}

	// zero some parts if needed
	if (msize) {
		if (vmm_map((void *)mempos, msize, VMM_FLAGS_EXACT, mmuflags, NULL) == NULL)
			return ENOMEM;
	}

	return 0;
}

int elf_load(vnode_t *vnode, void *base, void **entry, char **interpreter, auxv64list_t *auxv64) {
	// TODO return ETXTBUSY if node is open for writing
	if (vnode->type != V_TYPE_REGULAR)
		return EACCES;

	elfheader64_t header;
	int err = readexact(vnode, &header, sizeof(elfheader64_t), 0);
	if (err)
		return err;

	if (headerok(&header) == false)
		return ENOEXEC;

	auxv64->null.type = AT_NULL;
	auxv64->phdr.type = AT_PHDR;
	auxv64->phnum.type = AT_PHNUM;
	auxv64->phent.type = AT_PHENT;
	auxv64->entry.type = AT_ENTRY;
	
	auxv64->phnum.val = header.phcount;
	auxv64->phent.val = header.phsize;
	auxv64->entry.val = header.entry;
	*entry = (void *)(header.entry + (uintptr_t)base);

	__assert(header.phsize == sizeof(elfph64_t));

	size_t phtablesize = sizeof(elfph64_t) * header.phcount;
	elfph64_t *headers = alloc(phtablesize);
	if (headers == NULL)
		return ENOMEM;

	err = readexact(vnode, headers, phtablesize, header.phpos);
	if (err)
		goto cleanup;

	elfph64_t *interpreterph = NULL;

	for (int i = 0; i < header.phcount; ++i) {
		headers[i].memaddr += (uintptr_t)base;
		switch (headers[i].type) {
			case PH_TYPE_INTERPRETER:
				interpreterph = &headers[i];
				break;
			case PH_TYPE_PHDR:
				auxv64->phdr.val = headers[i].memaddr;
				break;
			case PH_TYPE_LOAD:
				err = load(vnode, &headers[i]);
				if (err)
					goto cleanup;
				break;
		}
	}

	if (interpreterph) {
		char *buff = alloc(interpreterph->fsize);
		if (buff == NULL) {
			err = ENOMEM;
			goto cleanup;
		}
		err = readexact(vnode, buff, interpreterph->fsize, interpreterph->offset);
		if (err)
			goto cleanup;
		*interpreter = buff;
	}

	cleanup:
	free(headers);
	return err;
}

#define STACK_MMUFLAGS (ARCH_MMU_FLAGS_READ | ARCH_MMU_FLAGS_WRITE | ARCH_MMU_FLAGS_NOEXEC | ARCH_MMU_FLAGS_USER)
#define STACK_SIZE (1024 * 1024 * 4)
#define STACK_TOP_BUFFER 8

void *elf_preparestack(void *top, auxv64list_t *auxv64, char **argv, char **envp) {
	size_t argc;
	size_t envc;
	size_t argdatasize = 0;
	size_t envdatasize = 0;

	for (argc = 0; argv[argc]; ++argc)
		argdatasize += strlen(argv[argc]) + 1;

	for (envc = 0; envp[envc]; ++envc)
		envdatasize += strlen(envp[envc]) + 1;

	// make sure stack will be 16 byte aligned on entry
	int alignment = ((argc + 1) + (envc + 1) + 1) & 1 ? 8 : 0;

	// env data + arg data + pointers to arg and env data + argc + buffer at the top
	size_t initialsize = argdatasize + envdatasize + (argc + envc) * sizeof(char *) + sizeof(size_t) + sizeof(auxv64list_t) + alignment + STACK_TOP_BUFFER;

	size_t initialsizeround = ROUND_UP(initialsize, PAGE_SIZE);
	void *initialpagebase = (void *)((uintptr_t)top - initialsizeround);
	if (vmm_map(initialpagebase, initialsizeround, VMM_FLAGS_ALLOCATE | VMM_FLAGS_EXACT, STACK_MMUFLAGS, NULL) == NULL)
		return NULL;

	void *stackbase = (void *)((uintptr_t)top - STACK_SIZE);
	size_t unallocatedsize = STACK_SIZE - initialsizeround;
	if (vmm_map(stackbase, unallocatedsize, VMM_FLAGS_EXACT, STACK_MMUFLAGS, NULL) == NULL)
		return NULL;

	top = (void *)((uintptr_t)top - STACK_TOP_BUFFER);

	char *argdatastart = (char *)((uintptr_t)top - argdatasize);
	char *envdatastart = (char *)((uintptr_t)argdatastart - envdatasize);
	auxv64list_t *auxvstart = (auxv64list_t *)envdatastart - 1;

	auxvstart = (auxv64list_t *)(((uintptr_t)auxvstart & ~(uintptr_t)0xf) - alignment);

	// also leave space for null entry
	char **envstart = (char **)auxvstart - envc - 1;
	char **argstart = (char **)envstart - argc - 1;
	size_t *argcptr = (size_t *)argstart - 1;

	memcpy(auxvstart, auxv64, sizeof(auxv64list_t));

	for (int i = 0; i < argc; ++i) {
		strcpy(argdatastart, argv[i]);
		*argstart++ = argdatastart;
		argdatastart += strlen(argv[i]) + 1;
	}

	for (int i = 0; i < envc; ++i) {
		strcpy(envdatastart, envp[i]);
		*envstart++ = envdatastart;
		envdatastart += strlen(envp[i]) + 1;
	}

	*argstart = NULL;
	*envstart = NULL;
	*argcptr = argc;

	return argcptr;
}
