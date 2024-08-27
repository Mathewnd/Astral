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

	size_t fdsbuffsize = nfds * sizeof(pollfd_t);
	pollfd_t *fdsbuff = alloc(fdsbuffsize);
	if (fdsbuff == NULL) {
		ret.errno = ENOMEM;
		return ret;
	}

	ret.errno = usercopy_fromuser(fdsbuff, fds, fdsbuffsize);
	if (ret.errno) {
		free(fdsbuff);
		return ret;
	}

	// we need to keep holding the files so another thread doesn't close them and break everything
	file_t **filebuff = alloc(sizeof(file_t *) * nfds);
	if (filebuff == NULL) {
		ret.errno = ENOMEM;
		goto cleanup;
	}

	polldesc_t desc = {0};
	// when nfds is 0, poll is used as a sleep.
	// therefore, initialize the desc size to one descriptor in that case.
	ret.errno = poll_initdesc(&desc, nfds == 0 ? 1 : nfds);
	if (ret.errno) {
		goto cleanup;
	}

	size_t eventcount = 0;

	for (uintmax_t i = 0; i < nfds; ++i) {
		fdsbuff[i].revents = 0;

		// negative fds are ignored and return nothing
		if (fdsbuff[i].fd < 0)
			continue;

		file_t *file = fd_get(fdsbuff[i].fd);
		if (file == NULL) {
			fdsbuff[i].revents = POLLNVAL;
			++eventcount;
			continue;
		}

		filebuff[i] = file;

		VOP_LOCK(file->vnode);
		int revents = VOP_POLL(file->vnode, timeoutms == 0 ? NULL : &desc.data[i], fdsbuff[i].events);
		VOP_UNLOCK(file->vnode);

		if (revents) {
			fdsbuff[i].revents = revents;
			++eventcount;
		}
	}

	if (eventcount == 0 && timeoutms != 0) {
		ret.errno = poll_dowait(&desc, (timeoutms == -1 ? 0 : timeoutms) * 1000);

		if (ret.errno == 0 && desc.event) {
			int fd = ((uintptr_t)desc.event - (uintptr_t)desc.data) / sizeof(polldata_t);
			fdsbuff[fd].revents = desc.data[fd].revents;
			eventcount = 1;
		}
	}

	if (ret.errno == 0)
		ret.errno = usercopy_touser(fds, fdsbuff, fdsbuffsize);

	poll_leave(&desc);
	poll_destroydesc(&desc);

	// release the files now
	for (uintmax_t i = 0; i < nfds; ++i) {
		if (filebuff[i])
			fd_release(filebuff[i]);
	}

	ret.ret = ret.errno ? -1 : eventcount;

	cleanup:
	if (filebuff)
		free(filebuff);

	free(fdsbuff);
	return ret;
}

syscallret_t syscall_ppoll(context_t *context, pollfd_t *fds, size_t nfds, timespec_t *utimeout, sigset_t *usigset) {
	timespec_t timeout;
	sigset_t sigset;
	sigset_t savedset;

	if (utimeout) {
		syscallret_t ret;
		ret.ret = -1;
		ret.errno = usercopy_fromuser(&timeout, utimeout, sizeof(timespec_t));
		if (ret.errno)
			return ret;
	}

	if (usigset) {
		syscallret_t ret;
		ret.ret = -1;
		ret.errno = usercopy_fromuser(&sigset, usigset, sizeof(sigset_t));
		if (ret.errno)
			return ret;

		signal_changemask(_cpu()->thread, SIG_SETMASK, &sigset, &savedset);
	}

	syscallret_t ret = syscall_poll(context, fds, nfds, utimeout ? (timeout.s * 1000 + timeout.ns / 1000000) : -1);

	if (usigset)
		signal_changemask(_cpu()->thread, SIG_SETMASK, &savedset, NULL);

	return ret;
}
