#include <kernel/jobctl.h>
#include <kernel/interrupt.h>
#include <kernel/tty.h>
#include <errno.h>
#include <logging.h>

// assumes interrupts are disabled
static void addtolist(proc_t *pgrp, proc_t *proc) {
	spinlock_acquire(&pgrp->pgrp.lock);

	proc->pgrp.nextmember = pgrp->pgrp.nextmember;
	proc->pgrp.previousmember = NULL;
	pgrp->pgrp.nextmember = proc;

	if (proc->pgrp.nextmember)
		proc->pgrp.nextmember->pgrp.previousmember = proc;

	spinlock_release(&pgrp->pgrp.lock);
}

// assumes interrupts are disabled
static void removefromlist(proc_t *pgrp, proc_t *proc) {
	spinlock_acquire(&pgrp->pgrp.lock);

	if (proc->pgrp.previousmember)
		proc->pgrp.previousmember->pgrp.nextmember = proc->pgrp.nextmember;

	if (proc->pgrp.nextmember)
		proc->pgrp.nextmember->pgrp.previousmember = proc->pgrp.previousmember;

	spinlock_release(&pgrp->pgrp.lock);
}

void jobctl_setctty(proc_t *proc, void *_ctty) {
	tty_t *tty = _ctty;
	__assert(tty);

	bool intstatus = interrupt_set(false);
	spinlock_acquire(&proc->jobctllock);

	tty_t *unholdtty = NULL;

	// if not the session leader
	if (proc->session.leader)
		goto leave;

	if (tty == NULL && proc->session.controllingtty) {
		unholdtty = proc->session.controllingtty;
		proc->session.controllingtty = NULL;
	}

	if (tty && proc->session.controllingtty == NULL) {
		VOP_HOLD(tty->mastervnode);
		proc->session.controllingtty = tty;
	}

	leave:
	spinlock_release(&proc->jobctllock);
	interrupt_set(intstatus);

	if (unholdtty) {
		VOP_RELEASE(unholdtty->mastervnode);
	}
}

void *jobctl_getctty(proc_t *proc) {
	tty_t *tty = NULL;

	bool intstatus = interrupt_set(false);
	spinlock_acquire(&proc->jobctllock);

	if (proc->session.leader) {
		proc_t *session = proc->session.leader;
		spinlock_acquire(&session->jobctllock);

		tty = session->session.controllingtty;
		if (tty) {
			VOP_HOLD(tty->mastervnode);
		}

		spinlock_release(&session->jobctllock);
	} else if (proc->session.controllingtty){
		// is the session leader
		tty = proc->session.controllingtty;
		VOP_HOLD(tty->mastervnode);
	}

	spinlock_release(&proc->jobctllock);
	interrupt_set(intstatus);
	return tty;
}

pid_t jobctl_getpgid(proc_t *proc) {
	bool intstatus = interrupt_set(false);
	spinlock_acquire(&proc->jobctllock);

	pid_t pid = proc->pgrp.leader ? proc->pgrp.leader->pid : proc->pid; 

	spinlock_release(&proc->jobctllock);
	interrupt_set(intstatus);

	return pid;
}

pid_t jobctl_getsid(proc_t *proc) {
	bool intstatus = interrupt_set(false);
	spinlock_acquire(&proc->jobctllock);

	pid_t pid = proc->session.leader ? proc->session.leader->pid : proc->pid;

	spinlock_release(&proc->jobctllock);
	interrupt_set(intstatus);

	return pid;
}

int jobctl_newsession(proc_t *proc) {
	// keep those saved so we don't unhold them with interrupts disabled,
	// which would possibly call into the allocators without interrupts
	proc_t *unholdpgrp = NULL;
	proc_t *unholdsession = NULL;

	int error = 0;
	bool intstatus = interrupt_set(false);
	spinlock_acquire(&proc->jobctllock);

	// check if session/group leader
	if (proc->pgrp.leader == NULL) {
		error = EPERM;
		goto leave;
	}

	unholdpgrp = proc->pgrp.leader;
	unholdsession = proc->session.leader;

	removefromlist(proc->pgrp.leader, proc);

	proc->session.leader = NULL;
	proc->session.foreground = NULL;
	proc->session.controllingtty = NULL;

	proc->pgrp.leader = NULL;
	proc->pgrp.nextmember = NULL;
	proc->pgrp.previousmember = NULL;

	leave:
	spinlock_release(&proc->jobctllock);
	interrupt_set(intstatus);

	if (unholdpgrp) {
		PROC_RELEASE(unholdpgrp);
		PROC_RELEASE(unholdsession);
	}

	return error;
}

int jobctl_newgroup(proc_t *proc) {
	// keep it saved so we don't unhold it with interrupts disabled,
	// which would possibly call into the allocators without interrupts
	proc_t *unholdpgrp = NULL;

	int error = 0;
	bool intstatus = interrupt_set(false);
	spinlock_acquire(&proc->jobctllock);

	// check if session/group leader
	if (proc->pgrp.leader == NULL) {
		error = EPERM;
		goto leave;
	}

	unholdpgrp = proc->pgrp.leader;

	removefromlist(proc->pgrp.leader, proc);

	proc->pgrp.leader = NULL;
	proc->pgrp.nextmember = NULL;
	proc->pgrp.previousmember = NULL;

	leave:
	spinlock_release(&proc->jobctllock);
	interrupt_set(intstatus);

	if (unholdpgrp) {
		PROC_RELEASE(unholdpgrp);
	}

	return error;
}

int jobctl_changegroup(proc_t *proc, proc_t *group) {
	// keep it saved so we don't unhold it with interrupts disabled,
	// which would possibly call into the allocators without interrupts
	proc_t *unholdpgrp = NULL;

	int error = 0;
	bool intstatus = interrupt_set(false);
	spinlock_acquire(&proc->jobctllock);

	// check if session/group leader
	if (proc->pgrp.leader == NULL) {
		error = EPERM;
		goto leave;
	}

	removefromlist(proc->pgrp.leader, proc);
	unholdpgrp = proc->pgrp.leader;
	proc->pgrp.leader = group;
	addtolist(group, proc);

	leave:
	spinlock_release(&proc->jobctllock);
	interrupt_set(intstatus);

	if (unholdpgrp) {
		PROC_RELEASE(unholdpgrp);
	}

	return error;
}

void jobctl_detach(proc_t *proc) {
	// called on process exit (when the last userland thread is exiting)
	proc_t *unholdpgrp = NULL;
	proc_t *unholdsession = NULL;
	bool intstatus = interrupt_set(false);
	spinlock_acquire(&proc->jobctllock);

	if (proc->pgrp.leader) {
		unholdpgrp = proc->pgrp.leader;
		removefromlist(proc->pgrp.leader, proc);
		proc->pgrp.leader = NULL;
	}

	if (proc->session.leader) {
		unholdsession = proc->session.leader;
		proc->session.leader = NULL;
	}

	spinlock_release(&proc->jobctllock);
	interrupt_set(intstatus);

	if (unholdpgrp) {
		PROC_RELEASE(unholdpgrp);
	}

	if (unholdsession) {
		PROC_RELEASE(unholdsession);
	}
}

void jobctl_addproc(proc_t *parent, proc_t *proc) {
	// called when a new process is being created (such as by fork)

	bool intstatus = interrupt_set(false);
	spinlock_acquire(&parent->jobctllock);
	spinlock_acquire(&proc->jobctllock);

	proc->session.leader = parent->session.leader ? parent->session.leader : parent;
	proc->pgrp.leader = parent->pgrp.leader ? parent->pgrp.leader : parent;

	PROC_HOLD(proc->session.leader);
	PROC_HOLD(proc->pgrp.leader);

	addtolist(proc->pgrp.leader, proc);

	spinlock_release(&proc->jobctllock);
	spinlock_release(&parent->jobctllock);
	interrupt_set(intstatus);
}

void jobctl_procremove(proc_t *proc) {
	// called after all refcounts to a proc are released
	// cleans up session stuff

	if (proc->session.leader != NULL)
		return;

	if (proc->session.foreground) {
		PROC_RELEASE(proc->session.foreground);
	}

	tty_t *tty = proc->session.controllingtty;
	VOP_RELEASE(tty->mastervnode);
}
