#include <kernel/sched.h>
#include <arch/cls.h>
#include <kernel/sched.h>
#include <arch/spinlock.h>
#include <kernel/alloc.h>
#include <arch/mmu.h>
#include <kernel/pmm.h>
#include <kernel/timer.h>
#include <string.h>
#include <kernel/elf.h>

#define PROC_START_FD_COUNT 3
#define THREAD_QUANTUM 10000

// queue 0: interrupt threads
// queue 1: kernel threads
// queue 2: user threads

#define QUEUE_COUNT 3

sched_queue queues[QUEUE_COUNT];
sched_queue running;
sched_queue blocked;

static proc_t* allocproc(size_t threadcount){
	
	proc_t* proc = alloc(sizeof(proc_t));
	if(!proc)
		return NULL;

	proc->threads = alloc(sizeof(thread_t*) * threadcount);
	
	if(!proc->threads){
		free(proc);
		return NULL;
	}

	proc->fds = alloc(sizeof(fd_t) * PROC_START_FD_COUNT);

	if(!proc->fds){
		free(proc->threads);
		free(proc);
		return NULL;
	}

	proc->fdcount = PROC_START_FD_COUNT;
	proc->firstfreefd = PROC_START_FD_COUNT;

	proc->context = vmm_newcontext();

	if(!proc->context){
		free(proc->threads);
		free(proc);
		return NULL;
	}
	
	return proc;
	
}

static thread_t* allocthread(proc_t* proc, state_t state, pid_t tid, size_t kstacksize){
	
	thread_t* thread = alloc(sizeof(thread_t));
	if(!thread)
		return NULL;


	thread->regs = alloc(sizeof(arch_regs));
	
	if(!thread->regs){
		free(thread);
		return NULL;
	}


	// XXX use alloc() when it's better
	
	size_t pagec = kstacksize / PAGE_SIZE + 1;
	
	thread->kernelstackbase = pmm_hhdmalloc(pagec);


	if(!thread->kernelstackbase){
		free(thread->regs);
		free(thread);
		return NULL;
	}

	thread->proc = proc;
	thread->state = state;
	thread->tid = tid;
	thread->kernelstack = thread->kernelstackbase + kstacksize;
	thread->stacksize = kstacksize;
	
	return thread;

}

static thread_t* freethread(thread_t* thread){
	free(thread->regs);
	// XXX use free() when it's better
	pmm_free(thread->kernelstackbase, thread->stacksize / PAGE_SIZE + 1);
	free(thread);
}

static void queue_add(sched_queue* queue, thread_t* thread){
	thread->next = NULL;
	thread->prev = queue->end;
	if(queue->end)
		queue->end->next = thread;
	queue->end = thread;
	if(!queue->start)
		queue->start = thread;

}

static void queue_remove(sched_queue* queue, thread_t* thread){	

	if(thread->next)
		thread->next->prev = thread->prev;
	else
		queue->end = thread->prev;

	if(thread->prev)
		thread->prev->next = thread->next;
	else
		queue->start = thread->next;

}

static thread_t* getnext(){
	
	thread_t* thread = NULL;

	for(int i = 0; i < QUEUE_COUNT && thread == NULL; ++i){
		
		spinlock_acquire(&queues[i].lock);
		
		if(queues[i].start){
			thread = queues[i].start;
			queue_remove(&queues[i], thread);
		}

		spinlock_release(&queues[i].lock);

	}

	if(thread){

		spinlock_acquire(&running.lock);
		
		queue_add(&running, thread);
		
		spinlock_release(&running.lock);

	}

	return thread;

}

int a = 0;

void sched_timerhook(arch_regs* regs){

	thread_t* current = arch_getcls()->thread;

	sched_queue* currentqueue = &queues[current->priority];


	spinlock_acquire(&running.lock);

	queue_remove(&running, current);

	spinlock_release(&running.lock);

	
	memcpy(current->regs, regs, sizeof(arch_regs));
	arch_regs_saveextra(&current->extraregs);

	spinlock_acquire(&currentqueue->lock);

	queue_add(currentqueue, current);

	spinlock_release(&currentqueue->lock);
	

	thread_t* next = getnext();
	
	if(next)
		memcpy(regs, next->regs, sizeof(arch_regs));

	timer_add(&arch_getcls()->schedreq, THREAD_QUANTUM, false);

	arch_getcls()->thread = next;

}

static void switch_thread(thread_t* thread){
	
	thread_t* current = arch_getcls()->thread;
	
	// if we don't have to change processes don't waste time
	
	if(current->proc != thread->proc){
		
		vmm_switchcontext(thread->proc->context);

	}
	
	arch_getcls()->thread = thread;

	arch_setkernelstack(thread->kernelstack);

	arch_regs_setupextra(&thread->extraregs);
	
	arch_switchcontext(thread->regs);
	
	__builtin_unreachable();

}

thread_t* sched_newuthread(void* ip, size_t kstacksize, void* stack, proc_t* proc, bool run, int prio){

	thread_t* thread = allocthread(NULL, THREAD_STATE_WAITING, 0, kstacksize);
	
	if(!proc){
		proc = allocproc(1);
		if(!proc){
			freethread(thread);
			return NULL;
		}
	}
	else{
		thread_t** tmp = realloc(proc->threads, sizeof(thread_t*) * (proc->threadcount + 1));
		if(!tmp){
			freethread(thread);
			return NULL;
		}
		proc->threads = tmp;
		++proc->threadcount;
	}
	
	thread->priority = prio;
	thread->proc = proc;
	arch_regs_setupuser(thread->regs, ip, stack, true);

	if(run)
		queue_add(&queues[prio], thread);

	return thread;

}

thread_t* sched_newkthread(void* ip, size_t stacksize, bool run, int prio){
	thread_t* thread = allocthread(NULL, THREAD_STATE_WAITING, 0, stacksize);

	if(!thread) return NULL;
	
	thread->priority = prio;

	arch_regs_setupkernel(thread->regs, ip, thread->kernelstack, true);

	if(run)
		queue_add(&queues[prio], thread);

	return thread;
}

void sched_init(){
	arch_getcls()->thread = allocthread(NULL, THREAD_STATE_RUNNING, 0, 0);
	arch_getcls()->thread->priority = THREAD_PRIORITY_KERNEL;

	timer_req* req = &arch_getcls()->schedreq;

	req->func = sched_timerhook;

}

void sched_runinit(){
	
	printf("Loading /sbin/init\n");

	thread_t* thread = sched_newuthread(NULL, PAGE_SIZE*3, NULL, NULL, true, THREAD_PRIORITY_USER);

	proc_t* proc = thread->proc;

	vmm_switchcontext(proc->context);

	proc->root = vfs_root();
	proc->cwd  = vfs_root();
	proc->pid  = 1;

	fd_t* stdinfd = &proc->fds[0];
	fd_t* stdoutfd = &proc->fds[1];
	fd_t* stderrfd = &proc->fds[2];
	
	#define CONSOLE_PATH "dev/console"

	int ret = vfs_open(&stdinfd->node, vfs_root(), CONSOLE_PATH);

	if(ret){
		printf("Open failed: %s\n", strerror(ret));
		_panic("Could not open stdin for init", 0);
	}

	ret = vfs_open(&stdoutfd->node, vfs_root(), CONSOLE_PATH);

	if(ret){
		printf("Open failed: %s\n", strerror(ret));
		_panic("Could not open stdout for init", 0);
	}

	ret = vfs_open(&stderrfd->node, vfs_root(), CONSOLE_PATH);

	if(ret){
		printf("Open failed: %s\n", strerror(ret));
		_panic("Could not open stderr for init", 0);
	}
	
	stdinfd->offset = stdoutfd->offset = stderrfd->offset = 0;
	stdinfd->flags = O_RDONLY + 1;
	stdoutfd->flags = stderrfd->flags = O_WRONLY + 1;

	vnode_t* node;
	ret = vfs_open(&node, vfs_root(), "sbin/init");

	if(ret){
		printf("Open failed: %s\n", strerror(ret));
		_panic("Could not load init", 0);
	}

	char* argv[] = {"/sbin/init", NULL};
	char* env[]  = {NULL};

	ret = elf_load(thread, node, argv, env);
	
	if(ret){
		printf("ELF load error: %s\n", strerror(ret));
		_panic("Could not load init", 0);
	}

	switch_thread(thread);
	
}
