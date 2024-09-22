#include <kernel/scheduler.h>
#include <arch/signal.h>
#include <logging.h>
#include <kernel/usercopy.h>

#define ACTION_TERM 0
#define ACTION_IGN 1
#define ACTION_CORE 2
#define ACTION_STOP 3
#define ACTION_CONT 4

int signal_defaultactions[NSIG] = {
	[SIGABRT] = ACTION_CORE,
	[SIGALRM] = ACTION_TERM,
	[SIGBUS] = ACTION_CORE,
	[SIGCHLD] = ACTION_IGN,
	[SIGCONT] = ACTION_CONT,
	[SIGFPE] = ACTION_CORE,
	[SIGHUP] = ACTION_TERM,
	[SIGILL] = ACTION_CORE,
	[SIGINT] = ACTION_TERM,
	[SIGIO] = ACTION_TERM,
	[SIGKILL] = ACTION_TERM,
	[SIGPIPE] = ACTION_TERM,
	[SIGPOLL] = ACTION_TERM,
	[SIGPROF] = ACTION_TERM,
	[SIGPWR] = ACTION_TERM,
	[SIGQUIT] = ACTION_CORE,
	[SIGSEGV] = ACTION_CORE,
	[SIGSTOP] = ACTION_STOP,
	[SIGTSTP] = ACTION_STOP,
	[SIGSYS] = ACTION_CORE,
	[SIGTERM] = ACTION_TERM,
	[SIGTRAP] = ACTION_CORE,
	[SIGTTIN] = ACTION_STOP,
	[SIGTTOU] = ACTION_STOP,
	[SIGURG] = ACTION_IGN,
	[SIGUSR1] = ACTION_TERM,
	[SIGUSR2] = ACTION_TERM,
	[SIGVTALRM] = ACTION_TERM,
	[SIGXCPU] = ACTION_CORE,
	[SIGXFSZ] = ACTION_CORE,
	[SIGWINCH] = ACTION_IGN
};

#define PROCESS_ENTER(proc) \
	bool procintstatus = interrupt_set(false); \
	spinlock_acquire(&proc->signals.lock);

#define PROCESS_LEAVE(proc) \
	spinlock_release(&proc->signals.lock); \
	interrupt_set(procintstatus);

#define THREAD_ENTER(thread) \
	bool threadintstatus = interrupt_set(false); \
	spinlock_acquire(&thread->signals.lock);

#define THREAD_LEAVE(thread) \
	spinlock_release(&thread->signals.lock); \
	interrupt_set(threadintstatus);

#define SIGNAL_ASSERT(s) __assert((s) < NSIG && (s) >= 0)

void signal_action(struct proc_t *proc, int signal, sigaction_t *new, sigaction_t *old) {
	SIGNAL_ASSERT(signal);
	PROCESS_ENTER(proc);

	if (old)
		*old = proc->signals.actions[signal];

	if (new)
		proc->signals.actions[signal] = *new;

	PROCESS_LEAVE(proc);
}

void signal_altstack(struct thread_t *thread, stack_t *new, stack_t *old) {
	THREAD_ENTER(thread);

	if (old)
		*old = thread->signals.stack;

	if (new)
		thread->signals.stack = *new;

	THREAD_LEAVE(thread);
}

void signal_changemask(struct thread_t *thread, int how, sigset_t *new, sigset_t *old) {
	THREAD_ENTER(thread);

	if (old)
		*old = thread->signals.mask;

	if (new) {
		switch (how) {
			case SIG_BLOCK:
			case SIG_UNBLOCK: {
				for (int i = 1; i < NSIG; ++i) {
					if (SIGNAL_GET(new, i) == 0)
						continue;

					if (how == SIG_BLOCK && SIGNAL_GET(&thread->signals.mask, i) == 0)
						SIGNAL_SETON(&thread->signals.mask, i);
					else if (how == SIG_UNBLOCK && SIGNAL_GET(&thread->signals.mask, i))
						SIGNAL_SETOFF(&thread->signals.mask, i);
				}
				break;
			}
			case SIG_SETMASK: {
				thread->signals.mask = *new;
				break;
			}
			default:
				__assert(!"bad how");
		}
	}

	THREAD_LEAVE(thread);
}

void signal_pending(struct thread_t *thread, sigset_t *sigset) {
	proc_t *proc = thread->proc;
	PROCESS_ENTER(proc);
	THREAD_ENTER(thread);

	// urgent set doesn't get returned here as it will always be handled
	// before a return to userspace
	memset(sigset, 0, sizeof(sigset_t));
	for (int i = 1; i < NSIG; ++i) {
		if (SIGNAL_GET(&proc->signals.pending, i))
			SIGNAL_SETON(sigset, i);
		
		if (SIGNAL_GET(&thread->signals.pending, i))
			SIGNAL_SETON(sigset, i);
	}

	THREAD_LEAVE(thread);
	PROCESS_LEAVE(proc);
}

void signal_returnmask(struct thread_t *thread, sigset_t *sigset) {
	memcpy(&thread->signals.returnmask, sigset, sizeof(sigset_t));
	thread->signals.hasreturnmask = true;
}

void signal_suspend(sigset_t *sigset) {
	thread_t *thread = current_thread();
	THREAD_ENTER(thread);

	memcpy(&thread->signals.mask, sigset, sizeof(sigset_t));
	signal_returnmask(thread, sigset);

	THREAD_LEAVE(thread);

	sched_preparesleep(true);
	sched_yield();
}

int signal_wait(sigset_t *sigset, timespec_t *timeout, siginfo_t *siginfo, int *signum) {
	thread_t *thread = current_thread();
	PROCESS_ENTER(thread->proc);
	THREAD_ENTER(thread);
	// if timeoutus is 0, this wait is just a poll
	// if timeout == NULL, this will sleep indefinitely
	// otherwise, use the timeout specified in timeout
	uintmax_t timeoutus = -1;
	int pending = 0;

	if (timeout)
		timeoutus = timeout->s * 1000000 + timeout->ns / 1000;

	for (int i = 1; i < NSIG; ++i) {
		if (SIGNAL_GET(sigset, i) == 0)
			continue;

		if (SIGNAL_GET(&thread->signals.pending, i)) {
			SIGNAL_SETOFF(&thread->signals.pending, i);
			pending = i;
			break;
		}

		if (SIGNAL_GET(&thread->proc->signals.pending, i)) {
			SIGNAL_SETOFF(&thread->proc->signals.pending, i);
			pending = i;
			break;
		}
	}

	if (pending) {
		// TODO change this to actually return the siginfo when its implemented
		memset(siginfo, 0, sizeof(siginfo_t));
		*signum = pending;

		THREAD_LEAVE(thread);
		PROCESS_LEAVE(thread->proc);
		return 0;
	}

	// signal is not pending and we are just polling
	// return EAGAIN
	if (pending == 0 && timeoutus == 0) {
		THREAD_LEAVE(thread);
		PROCESS_LEAVE(thread->proc);
		return EAGAIN;
	}

	memcpy(&thread->signals.waiting, sigset, sizeof(sigset_t));

	eventlistener_t eventlistener;
	EVENT_INITLISTENER(&eventlistener);

	EVENT_ATTACH(&eventlistener, &thread->signals.waitpendingevent);

	THREAD_LEAVE(thread);
	PROCESS_LEAVE(thread->proc);

	int err = EVENT_WAIT(&eventlistener, timeout ? timeoutus : 0);
	EVENT_DETACHALL(&eventlistener);

	// now one of the following has happened:
	// - the timeout expired
	// - the event has been signaled, which means there is a pending signal
	//   in the thread pending set which was in the waiting set
	// - the thread needs to return to userspace to service a signal (err == EINTR)

	if (err == 0) {
		PROCESS_ENTER(thread->proc);
		THREAD_ENTER(thread);
		// timeout or event happened
		// we still check for it even if it could have been a timeout,
		// as the lock wasn't held for a while which means that inbetween the
		// time out and the lock being held, a signal could've happened

		for (int i = 1; i < NSIG; ++i) {
			if (SIGNAL_GET(sigset, i) == 0)
				continue;

			if (SIGNAL_GET(&thread->signals.pending, i)) {
				SIGNAL_SETOFF(&thread->signals.pending, i);
				pending = i;
				break;
			}

			if (SIGNAL_GET(&thread->proc->signals.pending, i)) {
				SIGNAL_SETOFF(&thread->proc->signals.pending, i);
				pending = i;
				break;
			}
		}

		if (pending) {
			// there is a signal pending, and it has been removed from the pending set
			// TODO change this to actually return the siginfo when its implemented
			memset(siginfo, 0, sizeof(siginfo_t));
			*signum = pending;
		} else {
			// timed out and thre are no pending signals, return EAGAIN
			__assert(timeout);
			err = EAGAIN;
		}

		memset(&thread->signals.waiting, 0, sizeof(sigset_t));

		THREAD_LEAVE(thread);
		PROCESS_LEAVE(thread->proc);
	} else {
		PROCESS_ENTER(thread->proc);
		THREAD_ENTER(thread);

		memset(&thread->signals.waiting, 0, sizeof(sigset_t));

		THREAD_LEAVE(thread);
		PROCESS_LEAVE(thread->proc);
	}

	return err;
}

void signal_signalthread(struct thread_t *thread, int signal, bool urgent) {
	PROCESS_ENTER(thread->proc);
	THREAD_ENTER(thread);

	// TODO kill/stop/continue entire process if default actions call for it etc

	sigset_t *sigset = urgent ? &thread->signals.urgent : &thread->signals.pending;
	SIGNAL_SETON(sigset, signal);

	void *address = thread->proc->signals.actions[signal].address;
	bool notignored = address != SIG_IGN && ((address == SIG_DFL && signal_defaultactions[signal] != ACTION_IGN) || address != SIG_DFL);

	if (signal == SIGKILL || signal == SIGSTOP || urgent || (SIGNAL_GET(&thread->signals.mask, signal) == 0 && notignored)) {
		// signal cannot be ignored or its urgent or its not masked and not ignored
		sched_wakeup(thread, SCHED_WAKEUP_REASON_INTERRUPTED);
	} else if (notignored == false) {
		// ignored, remove from pending set
		SIGNAL_SETOFF(sigset, signal);
	} else if (notignored && SIGNAL_GET(&thread->signals.mask, signal) && SIGNAL_GET(&thread->signals.waiting, signal)) {
		// not ignored, masked and the thread is waiting on this signal
		EVENT_SIGNAL(&thread->signals.waitpendingevent);
	}

	THREAD_LEAVE(thread);
	PROCESS_LEAVE(thread->proc);
}

void signal_signalproc(struct proc_t *proc, int signal) {
	// first, check if any threads have the signal unmasked, and set as pending for the first one
	PROCESS_ENTER(proc);
	void *address = proc->signals.actions[signal].address;
	bool notignored = address != SIG_IGN && ((address == SIG_DFL && signal_defaultactions[signal] != ACTION_IGN) || address != SIG_DFL);
	bool notignorable = signal == SIGKILL || signal == SIGSTOP || signal == SIGCONT;
	bool threadcontinued = false;
	bool isdefaultaction = address == SIG_DFL;
	bool shouldstop = (isdefaultaction && signal_defaultactions[signal] == ACTION_STOP) || signal == SIGSTOP;

	// TODO if one thread has a stop signal masked, the masked wait breaks completely

	thread_t *thread = proc->threadlist;
	spinlock_acquire(&proc->threadlistlock);

	while (thread) {
		if (thread->flags & SCHED_THREAD_FLAGS_DEAD) {
			thread = thread->procnext;
			continue;
		}

		THREAD_ENTER(thread);

		if (SIGNAL_GET(&thread->signals.mask, signal) == 0 || notignorable) {
			SIGNAL_SETON(&thread->signals.pending, signal);
			if (notignored || notignorable) {
				int reason = SCHED_WAKEUP_REASON_INTERRUPTED;
				if (thread->signals.stopped && (signal == SIGCONT || signal == SIGKILL)) {
					// thread is stopped and we have to wake it up
					thread->signals.stopped = false;
					reason = 0; // wake it up from the non interruptible sleep
					threadcontinued = true;
				} else if (signal == SIGCONT) {
					// thread is not stopped, ignore SIGCONT
					SIGNAL_SETOFF(&thread->signals.pending, signal);
					goto threadleave;
				}

				sched_wakeup(thread, reason);
				// TODO ipi if running isn't self
			}

			// if we are stopping or continuing, this will be sent to all individual threads
			// XXX possible race condition where a thread will change the process mask or the action of stopping signals
			// after it was sent to everyone
			if ((shouldstop || signal == SIGCONT) == false) {
				THREAD_LEAVE(thread);
				PROCESS_LEAVE(proc);
				spinlock_release(&proc->threadlistlock);
				return;
			}
		}

		threadleave:
		THREAD_LEAVE(thread);
		thread = thread->procnext;
	}

	// no thread has it unmasked, see if any threads are waiting for the signal
	if (notignorable == false && (threadcontinued || shouldstop) == false) {
		thread_t *thread = proc->threadlist;
		while (thread) {
			THREAD_ENTER(thread);

			if (SIGNAL_GET(&thread->signals.waiting, signal)) {
				// thread is waiting for this signal, set it as pending in its pending set and
				// signal that it should wake up
				SIGNAL_SETON(&thread->signals.pending, signal);
				EVENT_SIGNAL(&thread->signals.waitpendingevent);

				THREAD_LEAVE(thread);
				spinlock_release(&proc->threadlistlock);
				PROCESS_LEAVE(proc);
				return;
			}

			THREAD_LEAVE(thread);
			thread = thread->procnext;
		}
	}

	spinlock_release(&proc->threadlistlock);

	// if its ignorable and there wasn't any thread with it unmasked nor waiting for it, set it as pending for the whole process
	// not ignorable signals will be sent to all threads individually
	if (notignorable == false && (threadcontinued || shouldstop) == false) {
		SIGNAL_SETON(&proc->signals.pending, signal);
	}

	PROCESS_LEAVE(proc);

	// tell the parent that a child stopped
	// TODO when stopping make sure that a thread was actually stopped
	// TODO protect parent with a spinlock
	if ((notignorable || notignored) && signal_defaultactions[signal] == ACTION_STOP) {
		proc->status = 0x7f;
		proc->status |= signal << 8;
		proc->signals.stopunwaited = true;

		if (proc->parent && (proc->parent->signals.actions[SIGCHLD].flags & SA_NOCLDSTOP) == 0) {
			signal_signalproc(proc->parent, SIGCHLD);
		}
		semaphore_signal(&proc->parent->waitsem);
	}

	// tell the parent that a child continued
	if (threadcontinued) {
		proc->status = 0xffff;
		proc->signals.continueunwaited = true;

		if (proc->parent && (proc->parent->signals.actions[SIGCHLD].flags & SA_NOCLDSTOP) == 0) {
			signal_signalproc(proc->parent, SIGCHLD);
		}

		semaphore_signal(&proc->parent->waitsem);
	}
}

// returns true if should retry check
bool signal_check(struct thread_t *thread, context_t *context, bool syscall, uint64_t syscallret, uint64_t syscallerrno) {
	bool retry = false;
	proc_t *proc = thread->proc;

	bool intstatus = interrupt_set(false);
	PROCESS_ENTER(proc);
	THREAD_ENTER(thread);

	if (SIGNAL_GET(&proc->signals.pending, SIGKILL) || SIGNAL_GET(&thread->signals.pending, SIGKILL)) {
		THREAD_LEAVE(thread);
		PROCESS_LEAVE(proc);
		interrupt_set(true);
		proc_terminate(SIGKILL);
	}

	// find the first pending signal which is unmasked

	int signal;
	sigset_t *sigset = NULL;

	// check the urgent sigset first
	for (int i = 1; i < NSIG; ++i) {
		if (SIGNAL_GET(&thread->signals.urgent, i) == 0)
			continue;

		signal = i;
		sigset = &thread->signals.urgent;
	}

	for (int i = 1; i < NSIG && sigset == NULL; ++i) {
		if (SIGNAL_GET(&thread->signals.mask, i))
			continue;

		if (SIGNAL_GET(&thread->signals.pending, i)) {
			signal = i;
			sigset = &thread->signals.pending;
			break;
		}

		if (SIGNAL_GET(&proc->signals.pending, i)) {
			signal = i;
			sigset = &proc->signals.pending;
			break;
		}
	}

	if (sigset == NULL)
		goto leave;

	sigaction_t *action = &proc->signals.actions[signal];
	SIGNAL_SETOFF(sigset, signal);

	// urgent signals cannot be ignored, if they are the default action will be run
	if (action->address == SIG_IGN && sigset != &thread->signals.urgent) {
		retry = true;
		goto leave;
	} else if ((action->address == SIG_DFL && (action->flags & SA_SIGINFO) == 0) || (action->address == SIG_IGN && sigset == &thread->signals.urgent)) {
		// default action
		int defaction = signal_defaultactions[signal];
		switch (defaction) {
			case ACTION_IGN:
			case ACTION_CONT:
				retry = true;
				goto leave;
			case ACTION_CORE:
			case ACTION_TERM:
				THREAD_LEAVE(thread);
				PROCESS_LEAVE(proc);
				interrupt_set(true);
				proc_terminate(signal);
			case ACTION_STOP:
				// TODO what if a thread unmasks a stop signal?
				// check if sigcont is not pending for the thread (SIGCONT is always sent to the thread)
				if (SIGNAL_GET(&thread->signals.pending, SIGCONT)) {
					retry = true;
					goto leave;
				}
				// if not, just sleep. the sigcont won't be sent until THREAD_LEAVE and preparesleep is protecting it from a lost wakeup
				context_t savecontext = *context; // save context as it could be overwritten if, for example, its the thread->context one
				sched_preparesleep(false);
				thread->signals.stopped = true;
				THREAD_LEAVE(thread);
				PROCESS_LEAVE(proc);
				sched_yield();
				interrupt_set(intstatus);
				// make sure a SIGCONT happened
				__assert(SIGNAL_GET(&thread->signals.pending, SIGCONT));
				__assert(thread->signals.stopped == false);
				*context = savecontext; // restore saved context
				// the stopped variable is already set by the signaling function
				// see if theres any other signal to deal with (such as the SIGCONT handler)
				return true;
			default:
				__assert(!"unsupported signal action");
		}
	} else {
		// execute handler
		ARCH_CONTEXT_THREADSAVE(thread, context);

		void *altstack = NULL;
		// figure out the stack we will be running the signal handler in
		if (action->flags & SA_ONSTACK && thread->signals.stack.size > 0)
			altstack = thread->signals.stack.base;

		// get where in memory to put the frame
		#if ARCH_SIGNAL_STACK_GROWS_DOWNWARDS == 1
		void *stack = altstack ? (void *)((uintptr_t)altstack + thread->signals.stack.size) : (void *)CTX_SP(context);
		stack = (void *)((((uintptr_t)stack - ARCH_SIGNAL_REDZONE_SIZE - sizeof(sigframe_t)) & ~0xfl) - 0x8);
		#else
			#error unsupported
		#endif

		// configure context for sigreturn() when a system call
		if (syscall) {
			CTX_ERRNO(context) = syscallerrno;
			CTX_RET(context) = syscallret;
			// TODO restartable system call stuff goes here
		}

		// configure stack frame
		sigframe_t sigframe;
		sigframe.restorer = action->restorer;
		if (altstack) {
			memcpy(&sigframe.oldstack, &thread->signals.stack, sizeof(stack_t));
			memset(&thread->signals.stack, 0, sizeof(stack_t));
		} else {
			memset(&sigframe.oldstack, 0, sizeof(stack_t));
		}

		if (thread->signals.hasreturnmask) {
			// if we were waiting in signal_suspend, push the old mask into the return frame
			// of the first signal and set it to not act this way the next check
			memcpy(&sigframe.oldmask, &thread->signals.returnmask, sizeof(sigset_t));
			thread->signals.hasreturnmask = false;
		} else {
			// else, just push the current mask
			memcpy(&sigframe.oldmask, &thread->signals.mask, sizeof(sigset_t));
		}
		memcpy(&sigframe.context, context, sizeof(context_t));
		memcpy(&sigframe.extracontext, &thread->extracontext, sizeof(extracontext_t));
		// TODO siginfo

		if (usercopy_touser(stack, &sigframe, sizeof(sigframe_t))) {
			printf("signal: bad user stack %p\n", stack);
			THREAD_LEAVE(thread);
			PROCESS_LEAVE(proc);
			interrupt_set(true);
			proc_terminate(SIGSEGV);
		}

		// configure new thread signal mask
		if ((action->flags & SA_NODEFER) == 0)
			SIGNAL_SETON(&thread->signals.mask, signal);

		for (int i = 1; i < signal; ++i) {
			if (SIGNAL_GET(&action->mask, i) == 0)
				continue;

			SIGNAL_SETON(&thread->signals.mask, i);
		}

		// configure return context
		memset(context, 0, sizeof(context_t));
		CTX_INIT(context, true, true);
		CTX_IP(context) = (uint64_t)action->address;
		CTX_SP(context) = (uint64_t)stack;
		CTX_ARG0(context) = signal;
		CTX_ARG1(context) = 0; // TODO pass siginfo
		CTX_ARG2(context) = 0; // TODO ucontext

		// reset handler if asked for
		if (action->flags & SA_RESETHAND)
			memset(action, 0, sizeof(sigaction_t));
	}

	leave:
	THREAD_LEAVE(thread);
	PROCESS_LEAVE(proc);
	interrupt_set(intstatus);
	return retry;
}
