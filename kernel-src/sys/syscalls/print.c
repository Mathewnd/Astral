#include <kernel/syscalls.h>
#include <printf.h>
#include <arch/e9.h>
#include <mutex.h>
#include <util.h>
#include <arch/cpu.h>

syscallret_t syscall_print(context_t *context, char *message) {
	syscallret_t ret = {
		.ret = -1
	};

	static MUTEX_DEFINE(mutex);

	size_t len;
	ret.errno = usercopy_strlen(message, &len);
	if (ret.errno)
		return ret;

	len = min(len, 1023);

	char msg[1024];
	ret.errno = usercopy_fromuser(msg, message, len);
	if (ret.errno)
		return ret;

	msg[len] = '\0';

	char prefix[100];
	snprintf(prefix, 100, "pid %d tid %d: ", current_thread()->proc->pid, current_thread()->tid);

	MUTEX_ACQUIRE(&mutex);
	arch_e9_puts(prefix);
	arch_e9_puts(msg);
	arch_e9_putc('\n');
	MUTEX_RELEASE(&mutex);

	ret.errno = 0;
	ret.ret = 0;
	return ret;
}
