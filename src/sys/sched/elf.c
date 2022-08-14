#include <kernel/elf.h>
#include <kernel/alloc.h>
#include <errno.h>

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

int elf_load(thread_t* thread, vnode_t* node, char** argv, char** env){
	
	elf_header64 header;	
	elf_ph64 ph;
	int err = 0;
	int readc = vfs_read(&err, node, &header, sizeof(elf_header64), 0);
	char* interp = NULL;

	if(err)
		return err;
	

	if(readc != sizeof(elf_header64) || (!check_header64(&header)))
		return EINVAL;
	
	
	// find out if this needs an interpreter

	for(size_t currentph = 0; currentph < header.ph_count; ++currentph){	
		readc = vfs_read(&err, node, &ph, header.ph_size, header.ph_pos + currentph*header.ph_size);
		if(err)
			return err;
		if(readc != header.ph_size)
			return EINVAL;
		
		if(ph.type == PH_TYPE_INTERPRETER){

			// load the name and elf header of the interpreter

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

			break;

		}
		
	}

	// load program into memory
	
	for(size_t currentph = 0; currentph < header.ph_count; ++currentph){
		
		readc = vfs_read(&err, node, &ph, header.ph_size, header.ph_pos + currentph*header.ph_size);

		if(err) goto _fail;

		if(readc != header.ph_size){
			err = EINVAL;
			goto _fail;
		}

		if(ph.type != PH_TYPE_LOAD)
			continue;


		
			
	}
	
	

	return 0;

	_fail:
		if(interp){
			free(interp);
			vfs_close(node);
		}
	
	return err;

}
