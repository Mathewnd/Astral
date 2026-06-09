#include <kernel/sysctl.h>
#include <kernel/proc.h>
#include <kernel/usercopy.h>
#include <kernel/alloc.h>
#include <kernel/jobctl.h>
#include <kernel/tty.h>

#define SYS_CTL_NAME_MAX 6

#define SYS_CTL_KERN 1
#define SYS_CTL_KERN_PROC 1
#define SYS_CTL_KERN_PROC_ALL 1

static void release_procs(proc_t **procs, size_t size) {
	for (size_t i = 0; i < size; ++i) {
		PROC_RELEASE(procs[i]);
	}
}

// TODO: fix size differences returning ENOMEM
static int ctl_proc(const int *name, size_t name_len, void *oldp, size_t *old_lenp, void *newp, size_t) {
	if (name_len != 4)
		return EINVAL;

	if (newp)
		return EPERM;

	size_t struct_size = min(name[2], sizeof(sysctl_proc_info_t));
	size_t proc_count = proc_get_count();
	size_t struct_count = proc_count;
	if (name[3])
		struct_count = min(struct_count, name[3]);

	if (!oldp) {
		// the caller just wants to know the size of the process table
		size_t new_len = struct_count * struct_size;
		return USERCOPY_POSSIBLY_TO_USER(old_lenp, &new_len, sizeof(new_len));
	}

	size_t old_len;
	int error = USERCOPY_POSSIBLY_FROM_USER(&old_len, old_lenp, sizeof(old_len));
	if (error)
		return error;

	if (old_len < struct_size * struct_count)
		return ENOMEM;

	proc_t **procs = alloc(sizeof(proc_t *) * struct_count);
	if (procs == NULL)
		return ENOMEM;

	struct_count = proc_get_table(procs, struct_count);
	size_t new_len = struct_count * struct_size;
	error = USERCOPY_POSSIBLY_TO_USER(old_lenp, &new_len, sizeof(new_len));
	if (error)
		goto cleanup;

	for (size_t i = 0; i < struct_count; ++i) {
		sysctl_proc_info_t proc_info;

		// TODO: this is currently very unsafe.
		proc_t *parent = procs[i]->parent;

		cred_getuids(&procs[i]->cred, &proc_info.uid, &proc_info.euid, &proc_info.suid);
		cred_getgids(&procs[i]->cred, &proc_info.gid, &proc_info.egid, &proc_info.sgid);

		strncpy(proc_info.name, procs[i]->name, sizeof(proc_info.name));

		proc_info.pid = procs[i]->pid;
		proc_info.thread_count = procs[i]->runningthreadcount;
		proc_info.ppid = parent ? parent->pid : 0;
		proc_info.pgid = jobctl_getpgid(procs[i]);
		proc_info.sid = jobctl_getsid(procs[i]);

		proc_info.start_timestamp = procs[i]->start_time;
		long ipl = spinlock_acquire_raise_ipl(&procs[i]->runtime_lock, IPL_DPC);
		proc_info.total_runtime = procs[i]->total_runtime;
		spinlock_release_lower_ipl(&procs[i]->runtime_lock, ipl);

		proc_info.state = 0;
		proc_info.cpu_time = 0;
		proc_info.nice = procs[i]->nice;
		proc_info.virtual_pages = 0;
		proc_info.physical_pages = 0;

		tty_t *ctty = jobctl_getctty(procs[i]);
		if (ctty) {
			strncpy(proc_info.tty_name, ctty->name, sizeof(proc_info.tty_name));
			tty_release(ctty);
		} else {
			memset(proc_info.tty_name, 0, sizeof(proc_info.tty_name));
		}

		error = USERCOPY_POSSIBLY_TO_USER(oldp + i * struct_size, &proc_info, struct_size);
		if (error)
			goto cleanup;
	}

	cleanup:
	release_procs(procs, struct_count);
	free(procs);
	return error;
}

int sysctl_kern(const int *name, size_t name_len, void *oldp, size_t *old_lenp, void *newp, size_t new_len) {
	switch (*name) {
		case SYS_CTL_KERN_PROC:
			return ctl_proc(name + 1, name_len - 1, oldp, old_lenp, newp, new_len);
	}

	return EINVAL;
}
