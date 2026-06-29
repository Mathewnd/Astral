#include <kernel/sysctl.h>
#include <kernel/proc.h>
#include <kernel/console.h>
#include <kernel/usercopy.h>
#include <kernel/alloc.h>
#include <kernel/jobctl.h>
#include <kernel/tty.h>
#include <arch/cpu.h>
#include <logging.h>

static void release_procs(proc_t **procs, size_t size) {
	for (size_t i = 0; i < size; ++i) {
		PROC_RELEASE(procs[i]);
	}
}

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

static int check_console_lock_permission(proc_t *proc) {
	if (CRED_IS_ESU(&proc->cred))
		return 0;

	tty_t *ctty = jobctl_getctty(proc);
	if (!ctty)
		return EPERM;

	bool is_console = console_is_tty(ctty);
	tty_release(ctty);
	if (!is_console)
		return EPERM;

	proc_t *session = jobctl_getsession(proc);
	bool allowed = session->cred.uid == proc->cred.euid;
	PROC_RELEASE(session);

	return allowed ? 0 : EPERM;
}

static int ctl_console_lock(const int *, size_t name_len, void *oldp, size_t *old_lenp, void *newp, size_t new_len) {
	if (name_len != 0)
		return EINVAL;

	if (oldp || old_lenp)
		return EINVAL;

	if (!newp || new_len != sizeof(int))
		return EINVAL;

	int value;
	int error = USERCOPY_POSSIBLY_FROM_USER(&value, newp, sizeof(value));
	if (error)
		return error;

	proc_t *proc = current_thread()->proc;
	bool lock = value != 0;
	if (lock) {
		error = check_console_lock_permission(proc);
		if (error)
			return error;

		error = console_set_lock(true);
		if (error)
			return error;

		__atomic_store_n(&proc->console_locked, true, __ATOMIC_SEQ_CST);
	} else {
		bool expected = true;
		if (!__atomic_compare_exchange_n(&proc->console_locked, &expected, false, false, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST))
			return EPERM;

		__assert(console_set_lock(false) == 0);
	}

	return 0;
}

int sysctl_kern(const int *name, size_t name_len, void *oldp, size_t *old_lenp, void *newp, size_t new_len) {
	switch (*name) {
		case SYS_CTL_KERN_PROC:
			return ctl_proc(name + 1, name_len - 1, oldp, old_lenp, newp, new_len);
		case SYS_CTL_KERN_CONSOLE_LOCK:
			return ctl_console_lock(name + 1, name_len - 1, oldp, old_lenp, newp, new_len);
	}

	return EINVAL;
}
