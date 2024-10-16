#include <kernel/syscalls.h>
#include <errno.h>
#include <kernel/scheduler.h>
#include <kernel/file.h>
#include <kernel/abi.h>

#define F_DUPFD  0
#define F_GETFD  1
#define F_SETFD  2
#define F_GETFL  3
#define F_SETFL  4

#define FD_CLOEXEC 1

syscallret_t syscall_fcntl(context_t *, int fd, int cmd, uint64_t arg) {
	syscallret_t ret = {
		.ret = -1
	};
	
	file_t *file = fd_get(fd);
	if (file == NULL) {
		ret.errno = EBADF;
		return ret;
	}

	switch (cmd) {
		case F_DUPFD:
			{
			int f;
			ret.errno = fd_dup(fd, arg, false, 0, &f);
			ret.ret = f;
			}
			break;
		case F_GETFL:
			// decrement to convert internal read/write flags to O_RDONLY etc
			ret.ret = file->flags - 1;
			break;
		case F_SETFL:
			FILE_LOCK(file);
			arg &= ~(O_RDONLY | O_WRONLY | O_RDWR | O_CREAT | O_EXCL | O_NOCTTY | O_TRUNC);	
			file->flags &= ~(O_APPEND | O_ASYNC | O_NOATIME | O_NONBLOCK);
			file->flags |= arg;
			ret.ret = 0;
			FILE_UNLOCK(file);
			break;
		case F_SETFD:
			ret.errno = fd_setflags(fd, (arg & FD_CLOEXEC) ? O_CLOEXEC : 0);
			ret.ret = 0;
			break;
		case F_GETFD:
			int flg;
			ret.errno = fd_getflags(fd, &flg);
			ret.ret = flg ? FD_CLOEXEC : 0;
			break;
		default:
			ret.errno = EINVAL;
			goto cleanup;
	}
	ret.errno = 0;

	cleanup:
	ret.ret = ret.errno ? -1 : ret.ret;

	fd_release(file);
	return ret;
}
