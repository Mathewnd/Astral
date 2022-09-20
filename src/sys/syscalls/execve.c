#include <kernel/syscalls.h>
#include <kernel/vmm.h>
#include <errno.h>
#include <kernel/alloc.h>
#include <string.h>
#include <kernel/vfs.h>
#include <kernel/sched.h>
#include <arch/spinlock.h>
#include <kernel/elf.h>
#include <arch/cls.h>
#include <arch/interrupt.h>

static void destroyvec(char* vec[], size_t vecsize){
	for(uintmax_t p = 0; p < vecsize; ++p){
		if(vec[p])
			free(vec[p]);
	}

	free(vec);

}

syscallret syscall_execve(const char* name, char* const argv[], char* const envp[]){
	
	syscallret retv;
	retv.ret = -1;
	
	if(name > USER_SPACE_END || argv > USER_SPACE_END || envp > USER_SPACE_END){
		retv.errno = EFAULT;
		return retv;
	}

	size_t namelen = strlen(name); // XXX user strlen
	size_t argc = 0;
	size_t envc = 0;

	// this probably should be checked in a safer way
	
	while(argv[argc]){
		++argc;
	}

	while(envp[envc]){
		++envc;
	}

	retv.errno = ENOMEM;

	char** argbuff = alloc((argc+1)*sizeof(char*));
	if(!argbuff)
		return retv;
	

	char** envbuff = alloc((envc+1)*sizeof(char*));

	if(!envbuff){
		free(argbuff);
		return retv;
	}

	char* namebuff = alloc(namelen+1);

	if(!namebuff){
		free(argbuff);
		free(envbuff);
		return retv;
	}

	strcpy(namebuff, name); // XXX user strcpy

	int err = 0;

	for(uintmax_t arg = 0; arg < argc && err == 0; ++arg){
		size_t len = strlen(argv[arg]); // XXX user strlen
		argbuff[arg] = alloc(len+1);
		if(!argbuff[arg]){
			err = ENOMEM;
			break;
		}
		strcpy(argbuff[arg], argv[arg]); // XXX user strcpy
	}

	for(uintmax_t env = 0; env < envc && err == 0; ++env){
		size_t len = strlen(envp[env]); // XXX user strlen
		envbuff[env] = alloc(len+1);
		if(!envbuff[env]){
			err = ENOMEM;
			break;
		}
		strcpy(envbuff[env], envp[env]); // XXX user strcpy	
	}

	if(err){
		retv.errno = err;
		goto _clean;
	}

	thread_t* thread = arch_getcls()->thread;

	vmm_context* oldctx = thread->ctx;
	vmm_context* ctx = vmm_newcontext();

	if(!ctx){
		retv.errno = ENOMEM;
		goto _clean;
	}

	vmm_switchcontext(ctx);
	
	spinlock_acquire(&thread->proc->lock);

	vnode_t* node;

	err = vfs_open(&node, namebuff[0] == '/' ? thread->proc->root : thread->proc->cwd, 
			namebuff);
	
	if(err){
		spinlock_release(&thread->proc->lock);
		retv.errno = err;
		return retv;
	}

	void *entry, *stack;

	err = elf_load(thread, node, argbuff, envbuff, &stack, &entry);

	if(err){
		vfs_close(node);
		spinlock_release(&thread->proc->lock);
		retv.errno = err;
		goto _clean;
	}

	arch_interrupt_disable();

	arch_regs_setupuser(thread->regs, entry, stack, true);
	
	// TODO stop other threads and destroy old context
		
	destroyvec(argbuff, argc+1);
	destroyvec(envbuff, envc+1);
	free(namebuff);
	vfs_close(node);
	spinlock_release(&thread->proc->lock);

	switch_thread(thread);

	_clean:
	
	destroyvec(argbuff, argc+1);
	destroyvec(envbuff, envc+1);
	free(namebuff);
	return retv;


}
