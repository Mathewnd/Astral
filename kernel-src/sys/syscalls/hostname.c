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

	if (unew)
		memcpy(new, unew, newsize);

	interrupt_set(false);
	spinlock_acquire(&lock);

	if (uold) {
		size_t copysize = min(oldsize - 1, bufferlen);

		memcpy(old, buffer, copysize);
		old[copysize] = '\0';
	}

	if (unew) {
		bufferlen = newsize;
		strcpy(buffer, new);
	}

	spinlock_release(&lock);
	interrupt_set(true);

	if (uold)
		strcpy(uold, old);

	ret.errno = 0;
	ret.ret = 0;
	return ret;
}
