#ifndef _SCHED_H_INCLUDE
#define _SCHED_H_INCLUDE

#include <sys/types.h>
#include <arch/regs.h>
#include <kernel/vfs.h>
#include <kernel/vmm.h>

#define THREAD_PRIORITY_INTERRUPT 0
#define THREAD_PRIORITY_KERNEL 1
#define THREAD_PRIORITY_USER 2

#define THREAD_STATE_WAITING 0
#define THREAD_STATE_RUNNING 1
#define THREAD_STATE_BLOCKED 2

typedef unsigned long state_t;

struct _proc_t;

typedef struct _thread_t{
	struct _thread_t* next;
	struct _thread_t* prev;
	struct _proc_t* proc;
	arch_regs* regs;
	arch_extraregs extraregs;
	state_t state;
	void* kernelstack;
	void* kernelstackbase;
	size_t stacksize;
	pid_t tid;
	int priority;
} thread_t;

typedef struct _proc_t{
	struct _proc_t* parent;
	struct _proc_t* sibling;
	struct _proc_t* child;
	pid_t pid;
	gid_t gid;
	uid_t uid;
	vmm_context* context;
	thread_t** threads;
	size_t threadcount;
	dirnode_t* root;
	dirnode_t* cwd;
} proc_t;


typedef struct{
	thread_t* start;
	thread_t* end;
	int lock;
} sched_queue;

void sched_init();

#endif
