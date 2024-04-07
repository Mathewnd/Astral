#include <kernel/syscalls.h>
#include <kernel/file.h>
#include <kernel/poll.h>
#include <kernel/vmm.h>
#include <logging.h>
#include <kernel/alloc.h>

typedef struct {
	int fd;
	short events;
	short revents;
} pollfd_t;

syscallret_t syscall_poll(context_t *, pollfd_t *fds, size_t nfds, int timeoutms) {
	syscallret_t ret = {
		.ret = -1
	};

	if ((void *)fds > USERSPACE_END) {
		ret.errno = EFAULT;
		return ret;
	}

	size_t fdsbuffsize = nfds * sizeof(pollfd_t);
	pollfd_t *fdsbuff = alloc(fdsbuffsize);
	if (fdsbuff == NULL) {
		ret.errno = ENOMEM;
		return ret;
	}

	memcpy(fdsbuff, fds, fdsbuffsize);

	// we need to keep holding the files so another thread doesn't close them and break everything
	file_t **filebuff = alloc(sizeof(file_t *) * nfds);
	if (filebuff == NULL) {
		ret.errno = ENOMEM;
		goto cleanup;
	}

	polldesc_t desc;
	// when nfds is 0, poll is used as a sleep.
	// therefore, initialize the desc size to one descriptor in that case.
	ret.errno = poll_initdesc(&desc, nfds == 0 ? 1 : nfds);
	if (ret.errno) {
		goto cleanup;
	}

	size_t eventcount = 0;

	for (uintmax_t i = 0; i < nfds; ++i) {
		// negative fds are ignored and return nothing
		if (fdsbuff[i].fd < 0) {
			fdsbuff[i].revents = 0;
			continue;
		}

		file_t *file = fd_get(fdsbuff[i].fd);
		if (file == NULL) {
			fdsbuff[i].revents = POLLNVAL;
			++eventcount;
			continue;
		}

		filebuff[i] = file;

		int revents = VOP_POLL(file->vnode, timeoutms == 0 ? NULL : &desc.data[i], fdsbuff[i].events);
		if (revents) {
			fdsbuff[i].revents = revents;
			++eventcount;
		}
	}

	if (eventcount == 0 && timeoutms != 0) {
		ret.errno = poll_dowait(&desc, (timeoutms == -1 ? 0 : timeoutms) * 1000);
		if (ret.errno == 0 && desc.event) {
			int fd = ((uintptr_t)desc.event - (uintptr_t)desc.data) / sizeof(pollfd_t);
			fdsbuff[fd].revents = desc.data[fd].revents;
			eventcount = 1;
		}
	}

	memcpy(fds, fdsbuff, fdsbuffsize);

	poll_leave(&desc);
	poll_destroydesc(&desc);

	// release the files now
	for (uintmax_t i = 0; i < nfds; ++i) {
		if (filebuff[i])
			fd_release(filebuff[i]);
	}

	ret.ret = eventcount;

	cleanup:
	if (filebuff)
		free(filebuff);

	free(fdsbuff);
	return ret;
}
