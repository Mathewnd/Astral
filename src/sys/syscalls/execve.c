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
#include <kernel/ustring.h>

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

	size_t namelen;
	retv.errno = u_strlen(name, &namelen);

	if(retv.errno)
		return retv;

	size_t argc = 0;
	size_t envc = 0;

	while(1){
		char* tmp;
		retv.errno = u_memcpy(&tmp, &argv[argc], sizeof(argv[argc]));
		if(!tmp)
			break;
		++argc;
	}
	
	while(1){
		char* tmp;
		retv.errno = u_memcpy(&tmp, &envp[envc], sizeof(envp[envc]));
		if(!tmp)
			break;
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

	int err = u_strcpy(namebuff, name);

	// XXX
	// this is still not that safe, later make it even safer.

	for(uintmax_t arg = 0; arg < argc && err == 0; ++arg){
		size_t len;
		err = u_strlen(argv[arg], &len);
		argbuff[arg] = alloc(len+1);
		if(!argbuff[arg]){
			err = ENOMEM;
			break;
		}
		u_strcpy(argbuff[arg], argv[arg]);
	}

	for(uintmax_t env = 0; env < envc && err == 0; ++env){
		size_t len;
		u_strlen(envp[env], &len);
		envbuff[env] = alloc(len+1);
		if(!envbuff[env]){
			err = ENOMEM;
			break;
		}
		u_strcpy(envbuff[env], envp[env]);
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
		goto _clean;
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
		
	// TODO destroy new context
	vmm_switchcontext(oldctx);
	destroyvec(argbuff, argc+1);
	destroyvec(envbuff, envc+1);
	free(namebuff);
	return retv;


}
