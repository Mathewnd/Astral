#ifndef _SYSCTL_H
#define _SYSCTL_H

#include <kernel/abi.h>
#include <time.h>

#define SYS_CTL_NAME_MAX 6

#define SYS_CTL_KERN 1
#define SYS_CTL_KERN_PROC 1
#define SYS_CTL_KERN_PROC_ALL 1

#define SYS_CTL_KERN_PROC_INFO_NAME_SIZE 64
#define SYS_CTL_KERN_PROC_INFO_TTY_NAME_SIZE 64

typedef struct {
	pid_t pid;
	uid_t uid;
	uid_t euid;
	uid_t suid;
	gid_t gid;
	gid_t egid;
	gid_t sgid;
	size_t thread_count;
	char name[SYS_CTL_KERN_PROC_INFO_NAME_SIZE];
	pid_t ppid;
	pid_t pgid;
	pid_t sid;
	timespec_t start_timestamp;
	timespec_t total_runtime;
	int state;
	int cpu_time;
	int nice;
	size_t virtual_pages;
	size_t physical_pages;
	char tty_name[SYS_CTL_KERN_PROC_INFO_TTY_NAME_SIZE];
} sysctl_proc_info_t;

int sysctl(const int *name, size_t name_len, void *oldp, size_t *old_lenp, void *newp, size_t new_len);
int sysctl_kern(const int *name, size_t name_len, void *oldp, size_t *old_lenp, void *newp, size_t new_len);

#endif
