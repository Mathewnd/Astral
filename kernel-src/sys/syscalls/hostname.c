#include <kernel/syscalls.h>
#include <kernel/abi.h>
#include <spinlock.h>
#include <util.h>

static spinlock_t lock;
static char buffer[HOST_NAME_MAX];
static size_t bufferlen;

void hostname_get(char *buf) {
	bool intstatus = interrupt_set(false);
	spinlock_acquire(&lock);

	memcpy(buf, buffer, bufferlen);
	buf[bufferlen] = '\0';

	spinlock_release(&lock);
	interrupt_set(intstatus);
}

syscallret_t syscall_hostname(context_t *, char *unew, size_t newsize, char *uold, size_t oldsize) {
	syscallret_t ret = {
		.ret = -1
	};

	if ((unew && newsize > HOST_NAME_MAX) || (uold && oldsize == 0)) {
		ret.errno = EINVAL;
		return ret;
	}

	char new[HOST_NAME_MAX];
	char old[HOST_NAME_MAX + 1];

	if (unew) {
		ret.errno = usercopy_fromuser(new, unew, newsize);
		if (ret.errno)
			return ret;
	}

	interrupt_set(false);
	spinlock_acquire(&lock);

	size_t copysize = min(oldsize - 1, bufferlen);
	if (uold) {
		memcpy(old, buffer, copysize);
		old[copysize] = '\0';
	}

	if (unew) {
		bufferlen = newsize;
		memcpy(buffer, new, newsize);
	}

	spinlock_release(&lock);
	interrupt_set(true);

	if (uold)
		ret.errno = usercopy_touser(uold, old, copysize + 1);

	ret.errno = 0;
	ret.ret = 0;
	return ret;
}
