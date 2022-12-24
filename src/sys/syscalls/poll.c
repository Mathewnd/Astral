#include <kernel/syscalls.h>
#include <kernel/alloc.h>
#include <kernel/vmm.h>
#include <arch/cls.h>
#include <poll.h>
#include <kernel/sched.h>
#include <arch/interrupt.h>
#include <arch/timekeeper.h>
#include <time.h>
#include <kernel/ustring.h>

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
	
	retv.errno = u_memcpy(ilist, fds, nfds*sizeof(pollfd));

	if(retv.errno){
		free(ilist);
		return retv;
	}

	for(size_t fd = 0; fd < nfds; ++fd){	
		ilist[fd].revents = 0;
	}

	// check for events and stuff

	bool eventhappened = false;

	fdtable_t* fdtable = &arch_getcls()->thread->proc->fdtable;
	
	struct timespec target = arch_timekeeper_gettime();
	target.tv_sec += timeoutms / 1000;
	target.tv_nsec += timeoutms * 1000000;
	if(target.tv_nsec >= 1000000000){
		target.tv_sec++;
		target.tv_nsec %= 1000000000;
	}

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
		if(timeoutms > 0){
			struct timespec now = arch_timekeeper_gettime();
			if(now.tv_sec > target.tv_sec)
				break;
			
			if(now.tv_sec == target.tv_sec && now.tv_nsec >= target.tv_sec)
				break;
			
		}
		arch_interrupt_disable();
		sched_yield();
		arch_interrupt_enable();

	}

	// copy results into each fd

	size_t eventcount = 0;

	for(uintmax_t i = 0; i < nfds; ++i){
		
		if(ilist[i].fd < 0)
			continue;
		
		if(u_memcpy(&fds[i].revents, &ilist[i].revents, sizeof(ilist[i].revents)))
				continue;
		
		
		if(ilist[i].revents)
			++eventcount;

	}

	retv.errno = 0;
	retv.ret = eventcount;

	ret:
	
	if(ilist)
		free(ilist);
	
	return retv;
}
