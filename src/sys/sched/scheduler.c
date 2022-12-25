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
#include <arch/interrupt.h>

#define THREAD_QUANTUM 10000

// queue 0: interrupt threads
// queue 1: kernel threads
// queue 2: user threads

#define QUEUE_COUNT 3

sched_queue queues[QUEUE_COUNT];

// XXX allocate these in a better way

static int pidlock;
static pid_t nextpid = 1;
static proc_t* init;

proc_t* sched_getinit(){
	return init;
}

static proc_t* allocproc(size_t threadcount){
	
	proc_t* proc = alloc(sizeof(proc_t));
	if(!proc)
		return NULL;

	proc->threads = alloc(sizeof(thread_t*) * threadcount);

	if(!proc->threads){
		free(proc);
		return NULL;
	}

	if(fd_tableinit(&proc->fdtable)){
		free(proc->threads);
		free(proc);
		return NULL;
	}

	spinlock_acquire(&pidlock);
	proc->pid = nextpid++;
	spinlock_release(&pidlock);
	
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

	
	thread->kernelstackbase = alloc(kstacksize);


	if(!thread->kernelstackbase){
		free(thread->regs);
		free(thread);
		return NULL;
	}


	thread->ctx = vmm_newcontext();

	if(!thread->ctx){
		free(thread->kernelstackbase);
		free(thread->regs);
		free(thread);
		return NULL;
	}

	thread->proc = proc;
	thread->state = state;
	thread->tid = tid;
	thread->kernelstack = thread->kernelstackbase + kstacksize;
	thread->stacksize = kstacksize;
	
	arch_regs_firsttimesetup(thread->regs, &thread->extraregs);

	return thread;

}

static thread_t* freethread(thread_t* thread){
	free(thread->regs);
	free(thread->kernelstack);
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

	while(thread == NULL){

		for(int i = 0; i < QUEUE_COUNT && thread == NULL; ++i){
			
			spinlock_acquire(&queues[i].lock);
			
			if(queues[i].start){
				thread = queues[i].start;
				queue_remove(&queues[i], thread);
			}

			spinlock_release(&queues[i].lock);

		}

		if(!thread){
			arch_interrupt_enable();
			arch_halt();
			arch_interrupt_disable();

		}

	}
	
	return thread;

}

void sched_queuethread(thread_t* thread){
	
	sched_queue* queue = &queues[thread->priority];
	
	arch_interrupt_disable();
	spinlock_acquire(&queue->lock);
	
	queue_add(queue, thread);
	
	spinlock_release(&queue->lock);
	arch_interrupt_enable();
}

void sched_timerhook(arch_regs* regs){


	#ifdef __X86_64__
	
	apic_eoi();

	#endif

	thread_t* current = arch_getcls()->thread;

	sched_queue* currentqueue = &queues[current->priority];
		
	memcpy(current->regs, regs, sizeof(arch_regs));
	arch_regs_saveextra(&current->extraregs);

	spinlock_acquire(&currentqueue->lock);

	queue_add(currentqueue, current);

	spinlock_release(&currentqueue->lock);

	thread_t* next = getnext();

	arch_getcls()->thread = next;
	

	if(next){
		memcpy(regs, next->regs, sizeof(arch_regs));
	}
	
	if(next->ctx != current->ctx)
		vmm_switchcontext(next->ctx);	


	arch_setkernelstack(next->kernelstack);
	arch_regs_setupextra(&next->extraregs);

	timer_add(&arch_getcls()->schedreq, THREAD_QUANTUM, false);




}

void switch_thread(thread_t* thread){
	
	thread_t* current = arch_getcls()->thread;
	
	// if we don't have to change address spaces don't waste time
	
	arch_getcls()->thread = thread;

	if(thread->ctx != current->ctx)
		vmm_switchcontext(thread->ctx);

	arch_setkernelstack(thread->kernelstack);

	arch_regs_setupextra(&thread->extraregs);

	arch_switchcontext(thread->regs);
	
	__builtin_unreachable();

}

thread_t* sched_newuthread(void* ip, size_t kstacksize, void* stack, proc_t* proc, bool run, int prio){

	thread_t* thread = allocthread(NULL, THREAD_STATE_RUNNING, 0, kstacksize);
	
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

	if(run){
		arch_interrupt_disable();
		spinlock_acquire(&queues[prio].lock);
		queue_add(&queues[prio], thread);
		spinlock_release(&queues[prio].lock);
		arch_interrupt_enable();
	}

	return thread;

}

thread_t* sched_newkthread(void* ip, size_t stacksize, bool run, int prio){
	thread_t* thread = allocthread(NULL, THREAD_STATE_RUNNING, 0, stacksize);

	if(!thread) return NULL;
	
	thread->priority = prio;

	arch_regs_setupkernel(thread->regs, ip, thread->kernelstack, true);

	if(run){
		arch_interrupt_disable();
		spinlock_acquire(&queues[prio].lock);
		queue_add(&queues[prio], thread);
		spinlock_release(&queues[prio].lock);
		arch_interrupt_enable();
	}
	return thread;
}

void sched_yieldtrampoline(thread_t* thread){
	
	arch_regs_saveextra(&thread->extraregs);
		
	if(thread->state == THREAD_STATE_RUNNING){
		spinlock_acquire(&queues[thread->priority].lock);
		queue_add(&queues[thread->priority], thread);
		spinlock_release(&queues[thread->priority].lock);
	}


	thread_t* nthread = getnext();
	
	timer_resume();
	
	switch_thread(nthread);

}

void arch_sched_yieldtrampoline(thread_t* thread, arch_regs* regs);

// these 3 expect interrupts to be disabled in entry

void sched_yield(){
	
	timer_stop();

	thread_t* thread = arch_getcls()->thread;

	arch_sched_yieldtrampoline(thread, thread->regs);

}

void sched_eventsignal(event_t* event, thread_t* thread){

	if(thread->state != THREAD_STATE_BLOCKED && thread->state != THREAD_STATE_BLOCKED_INTR)
		return;

	if(thread->state == THREAD_STATE_BLOCKED_INTR && event == &thread->sigevent)
		return;
	
	thread->state = THREAD_STATE_RUNNING;
	thread->awokenby = event;
	
	spinlock_acquire(&queues[thread->priority].lock);
	queue_add(&queues[thread->priority], thread);
	spinlock_release(&queues[thread->priority].lock);
}

void sched_dequeue(){

	timer_stop();
	arch_interrupt_disable();	

	thread_t* thread = getnext();
	
	timer_resume();

	// for safety


	arch_getcls()->thread = thread;
	vmm_switchcontext(thread->ctx);
	
	switch_thread(thread);
	

}

__attribute__((noreturn)) int sched_die(){
	
	arch_getcls()->thread->state = THREAD_STATE_DEAD;

	sched_dequeue();
}

void sched_threadexitcheck(){
	if(arch_getcls()->thread->shouldexit)
		sched_die();
}


void sched_block(bool interruptible){
	
	thread_t* thread = arch_getcls()->thread;
	
	thread->state = interruptible ? THREAD_STATE_BLOCKED_INTR : THREAD_STATE_BLOCKED;
	
	sched_yield();

}

void sched_init(){
	arch_getcls()->thread = allocthread(NULL, THREAD_STATE_RUNNING, 0, 0);
	arch_getcls()->thread->priority = THREAD_PRIORITY_KERNEL;

	timer_req* req = &arch_getcls()->schedreq;
	
	req->func = sched_timerhook;
}

void sched_runinit(){
	
	printf("Loading /sbin/init\n");

	thread_t* thread = sched_newuthread(NULL, PAGE_SIZE*10, NULL, NULL, false, THREAD_PRIORITY_USER);
	
	proc_t* proc = thread->proc;
	init = proc;

	arch_getcls()->thread = thread;

	vmm_switchcontext(thread->ctx);

	vfs_open(&proc->root, vfs_root(), "/");	
	vfs_open(&proc->cwd, vfs_root(), "/");	

	int tmp;

	fd_t *stdinfd, *stdoutfd, *stderrfd;

	fd_alloc(&proc->fdtable, &stdinfd, &tmp, 0);
	fd_alloc(&proc->fdtable, &stdoutfd, &tmp, 0);
	fd_alloc(&proc->fdtable, &stderrfd, &tmp, 0);
	
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
	ret = vfs_open(&node, vfs_root(), "/sbin/init");

	if(ret){
		printf("Open failed: %s\n", strerror(ret));
		_panic("Could not load init", 0);
	}

	char* argv[] = {"/sbin/init", NULL};
	char* env[]  = {NULL};
	
	void *entry, *stack;

	ret = elf_load(thread, node, argv, env, &stack, &entry);
	
	if(ret){
		printf("ELF load error: %s\n", strerror(ret));
		_panic("Could not load init", 0);
	}
	
	vfs_close(node);

	arch_regs_setupuser(thread->regs, entry, stack, true);

	fd_release(stdinfd);
	fd_release(stdoutfd);
	fd_release(stderrfd);
	
	proc->umask = 022;

	timer_add(&arch_getcls()->schedreq, THREAD_QUANTUM, true);
	
	switch_thread(thread);
	
	__builtin_unreachable();

}
