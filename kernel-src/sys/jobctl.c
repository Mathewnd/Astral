#include <kernel/jobctl.h>
#include <kernel/interrupt.h>
#include <kernel/tty.h>
#include <kernel/auth.h>
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
	else
		pgrp->pgrp.nextmember = NULL;

	if (proc->pgrp.nextmember)
		proc->pgrp.nextmember->pgrp.previousmember = proc->pgrp.previousmember;

	spinlock_release(&pgrp->pgrp.lock);
}

int jobctl_setctty(proc_t *proc, void *_ctty, bool steal) {
	tty_t *tty = _ctty;
	int error = 0;
	bool intstatus = interrupt_set(false);
	spinlock_acquire(&proc->jobctllock);

	tty_t *unholdtty = NULL;

	// if not the session leader
	if (proc->session.leader)
		goto leave;

	tty_t *checktty = proc->session.controllingtty;
	if (tty == NULL && checktty) {
		if (checktty)
			spinlock_acquire(&checktty->sessionlock);

		// check if it was stolen
		if (checktty->session == proc) {
			// not stolen, unset it
			// TODO SIGHUP and SIGCONT to foregroup group
			unholdtty = checktty;
			proc->session.controllingtty = NULL;
			checktty->session = NULL;
		}

		spinlock_release(&checktty->sessionlock);
	}

	if (tty && proc->session.controllingtty == NULL) {
		spinlock_acquire(&tty->sessionlock);

		if (tty->session == NULL) {
			VOP_HOLD(tty->mastervnode);
			proc->session.controllingtty = tty;
			tty->session = proc;
		} else if (steal) {
			// vnode reference gets transferred over to the new proc
			tty->session->session.controllingtty = NULL;
			tty->session = proc;
		} else {
			error = EPERM;
		}

		spinlock_release(&tty->sessionlock);
	}

	leave:
	spinlock_release(&proc->jobctllock);
	interrupt_set(intstatus);

	if (unholdtty) {
		VOP_RELEASE(unholdtty->mastervnode);
	}

	return error;
}

void *jobctl_getctty(proc_t *proc) {
	tty_t *tty = NULL;

	bool intstatus = interrupt_set(false);
	spinlock_acquire(&proc->jobctllock);
	proc_t *session = proc->session.leader ? proc->session.leader : proc;

	if (session != proc)
		spinlock_acquire(&session->jobctllock);

	tty = session->session.controllingtty;
	if (tty) {
		VOP_HOLD(tty->mastervnode);
	}

	if (session != proc)
		spinlock_release(&session->jobctllock);

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

	// check if already group leader
	if (proc->pgrp.leader == NULL)
		goto leave;

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
	PROC_HOLD(group);
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
	proc_t *foreground = NULL;

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
	} else {
		// send sighup and sigcont to all processes in the foreground group
		foreground = proc->session.foreground ? proc->session.foreground : proc;
		PROC_HOLD(foreground);
	}

	spinlock_release(&proc->jobctllock);
	interrupt_set(intstatus);

	if (unholdpgrp) {
		PROC_RELEASE(unholdpgrp);
	}

	if (unholdsession) {
		PROC_RELEASE(unholdsession);
	} else {
		jobctl_setctty(proc, NULL, 0);
		jobctl_signal(foreground, SIGHUP, NULL);
		jobctl_signal(foreground, SIGCONT, NULL);
		PROC_RELEASE(foreground);
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

proc_t *jobctl_getforeground(proc_t *proc) {
	proc_t *foreground = NULL;

	bool intstatus = interrupt_set(false);
	spinlock_acquire(&proc->jobctllock);

	if (proc->session.leader)
		goto leave;

	foreground = proc->session.foreground ? proc->session.foreground : proc;
	PROC_HOLD(foreground);

	leave:
	spinlock_release(&proc->jobctllock);
	interrupt_set(intstatus);

	return foreground;
}
int jobctl_setforeground(proc_t *session, proc_t *pgrp) {
	proc_t *unhold = NULL;
	int error = 0;

	bool intstatus = interrupt_set(false);
	spinlock_acquire(&session->jobctllock);
	if (session->session.leader) {
		error = EINVAL;
		goto leave;
	}

	if (session != pgrp) {
		spinlock_acquire(&pgrp->jobctllock);
		if (pgrp->pgrp.leader || pgrp->session.leader != session) {
			error = EPERM;
			goto leave;
		}
	}

	if (session->session.foreground)
		unhold = session->session.foreground;

	session->session.foreground = pgrp;
	PROC_HOLD(pgrp);

	leave:
	if (session != pgrp)
		spinlock_release(&pgrp->jobctllock);

	spinlock_release(&session->jobctllock);
	interrupt_set(intstatus);

	if (unhold) {
		PROC_RELEASE(unhold);
	}

	return error;
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

// proc can be null if the signal is being sent to the calling processes own process group
// since that is only passed from a userland thread, we can assume current_thread()->proc is not null
int jobctl_signal(proc_t *proc, int signal, proc_t *sender) {
	int error = 0;
	bool intstatus = interrupt_set(false);
	proc_t *leader;
	proc_t *tmpproc = current_thread()->proc;
	pid_t senderpgid = sender ? jobctl_getpgid(sender) : 0;

	if (proc == NULL) {
		spinlock_acquire(&tmpproc->jobctllock);
		leader = tmpproc->pgrp.leader ? tmpproc->pgrp.leader : tmpproc;
	} else {
		spinlock_acquire(&proc->jobctllock);
		if (proc->pgrp.leader) {
			error = ESRCH;
			spinlock_release(&proc->jobctllock);
			goto leave;
		}

		leader = proc;
		spinlock_release(&proc->jobctllock);
	}

	spinlock_acquire(&leader->pgrp.lock);

	size_t donecount = 0;
	proc_t *iterator = leader;
	while (iterator) {
		// only send the signal if:
		// there isn't a specific sender (for example, ^C from the tty)
		// the signal is SIGCONT and they are from the same process group
		// the auth check passes
		if (senderpgid == 0 || (signal == SIGCONT && leader->pid == senderpgid) || auth_process_check(&sender->cred, AUTH_ACTIONS_PROCESS_SIGNAL, iterator) == 0) {
			signal_signalproc(iterator, signal);
			++donecount;
		}

		iterator = iterator->pgrp.nextmember;
	}
	error = donecount == 0 ? EPERM : 0;

	spinlock_release(&leader->pgrp.lock);

	leave:
	if (proc == NULL) {
		spinlock_release(&tmpproc->jobctllock);
	}
	interrupt_set(intstatus);
	return error;
}
