#ifndef _SCHED_H_INCLUDE
#define _SCHED_H_INCLUDE

#include <sys/types.h>
#include <arch/regs.h>
#include <kernel/vfs.h>

#define THREAD_STATE_WAITING 0
#define THREAD_STATE_RUNNING 1
#define THREAD_STATE_BLOCKED 2

struct _proc_t;

typedef struct _thread_t{
	struct _thread_t* next;
	struct _proc_t* proc;
	arch_regs* regs;
	size_t state;
	void* kernelstack;
} thread_t;

typedef struct _proc_t{
	struct _proc_t* parent;
	struct _proc_t* sibling;
	struct _proc_t* child;
	pid_t pid;
	gid_t gid;
	uid_t uid;
	dirnode_t* root;
	dirnode_t* cwd;
} proc_t;

#endif
