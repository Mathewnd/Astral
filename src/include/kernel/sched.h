#ifndef _SCHED_H_INCLUDE
#define _SCHED_H_INCLUDE


#include <kernel/event.h>
#include <sys/types.h>
#include <arch/regs.h>
#include <kernel/vfs.h>
#include <kernel/vmm.h>
#include <kernel/fd.h>

#define THREAD_PRIORITY_INTERRUPT 0
#define THREAD_PRIORITY_KERNEL 1
#define THREAD_PRIORITY_USER 2

#define THREAD_STATE_WAITING 0
#define THREAD_STATE_RUNNING 1
#define THREAD_STATE_BLOCKED 2
#define THREAD_STATE_BLOCKED_INTR 3

#define PROC_STATE_NORMAL 0
#define PROC_STATE_ZOMBIE 1

typedef unsigned long state_t;

struct _proc_t;

typedef struct _thread_t{
	struct _thread_t* next;
	struct _thread_t* prev;
	struct _proc_t* proc;
	arch_regs* regs;
	arch_extraregs extraregs;
	state_t state;
	event_t  sigevent;
	event_t* awokenby;
	void* kernelstack;
	void* kernelstackbase;
	size_t stacksize;
	vmm_context* ctx;
	pid_t tid;
	int priority;
} thread_t;

typedef struct _proc_t{
	int lock;
	int state;
	int status;
	struct _proc_t* parent;
	struct _proc_t* sibling;
	struct _proc_t* child;
	pid_t pid;
	gid_t gid;
	uid_t uid;
	fdtable_t fdtable; 
	thread_t** threads;
	size_t threadcount;
	dirnode_t* root;
	dirnode_t* cwd;
	event_t childevent;
	mode_t umask;
} proc_t;


typedef struct{
	thread_t* start;
	thread_t* end;
	int lock;
} sched_queue;

void sched_dequeue();
proc_t* sched_getinit();
thread_t* sched_newuthread(void* ip, size_t stacksize, void* stack, proc_t* proc, bool run, int prio);
thread_t* sched_newkthread(void* ip, size_t stacksize, bool run, int prio);
void sched_queuethread(thread_t* thread);
void sched_init();
void sched_runinit();
void sched_eventsignal(event_t* event, thread_t* thread);
void sched_block(bool interruptible);
void sched_yield();
#endif
