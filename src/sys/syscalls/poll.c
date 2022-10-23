#include <kernel/syscalls.h>
#include <kernel/alloc.h>
#include <kernel/vmm.h>
#include <arch/cls.h>
#include <poll.h>
#include <kernel/sched.h>
#include <arch/interrupt.h>

#define MAXNFDS 4096

syscallret syscall_poll(pollfd *fds, size_t nfds, int timeoutms){
	syscallret retv;
	retv.ret = -1;

	if(fds > USER_SPACE_END){
		retv.errno = EFAULT;
		return retv;
	}

	if(nfds > MAXNFDS){
		retv.errno = EINVAL;
		return retv;
	}
	

	// create the internal list

	pollfd* ilist = alloc(nfds * sizeof(pollfd));

	if(!ilist){
		retv.errno = ENOMEM;
		return retv;
	}
	
	for(size_t fd = 0; fd < nfds; ++fd){	
		ilist[fd].fd = fds[fd].fd;
		ilist[fd].events = fds[fd].events;
	}

	// check for events and stuff

	bool eventhappened = false;

	fdtable_t* fdtable = &arch_getcls()->thread->proc->fdtable;

	while(!eventhappened){
		for(uintmax_t i = 0; i < nfds; ++i){
			if(ilist[i].fd < 0 || ilist[i].events == 0)
				continue;
			
			// open fd
			
			fd_t* fd;

			int err = fd_access(fdtable, &fd, ilist[i].fd);

			if(err){
				ilist[i].revents = POLLNVAL;
				eventhappened = true;
				continue;
			}
			
			// poll
			
			if(fd->node){
				if(vfs_poll(fd->node, &ilist[i]))
					ilist[i].revents = POLLERR;
			}

			if(ilist[i].revents)
				eventhappened = true;

			// cleanup
			
			fd_release(fd);
			
		}	
		
		// TODO check for signal or timeout
		
		if(timeoutms == 0) break;
		arch_interrupt_disable();
		sched_yield();
		arch_interrupt_enable();

	}

	// copy results into each fd

	size_t eventcount = 0;

	for(uintmax_t i = 0; i < nfds; ++i){
		
		if(ilist[i].fd < 0 || ilist[i].revents == 0)
			continue;

		fds[i].revents = ilist[i].revents;
		
		++eventcount;

	}

	retv.errno = 0;
	retv.ret = eventcount;

	ret:
	
	if(ilist)
		free(ilist);
	
	return retv;
}
