#include <kernel/syscalls.h>
#include <arch/cpu.h>

syscallret_t syscall_getcpu(context_t *) {
	syscallret_t ret = {
		.errno = 0
	};


	uint32_t cpu_id = current_cpu_internal_id();

	ret.ret = cpu_id;
	return ret;
}
