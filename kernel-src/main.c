#include <arch/cpu.h>
#include <kernel/init.h>
#include <logging.h>
#include <kernel/kernel_args.h>
#include <kernel/vfs.h>
#include <kernel/devfs.h>
#include <kernel/initrd.h>
#include <kernel/scheduler.h>

static cpu_t bsp_cpu;

INIT_ROUTINE_DEFINE(bsp_early,
		INIT_ROUTINE_FLAGS_PHONY,
		NULL,
		alloc, term, cpu
	);

DEFINE_KERNEL_ARGUMENT(root, char *);
DEFINE_KERNEL_ARGUMENT(rootfs, char *);
DEFINE_KERNEL_ARGUMENT(initrd, bool);

void kernel_entry() {
	cpu_set(&bsp_cpu);
	logging_sethook(arch_early_log);

	kernel_arguments_parse();

	init_run_routine(INIT_GET_ROUTINE(bsp_early));

	init_run_all_routines();

	// kernel is fully up and running by now
	// mount root filesystem

	char *root   = GET_KERNEL_ARGUMENT(root, char *);
	char *rootfs = GET_KERNEL_ARGUMENT(rootfs, char *);

	printf("entry: mounting %s (%s) on /\n", root == NULL ? "none" : root, rootfs);

	vnode_t *backing;
	if (root) {
		__assert(devfs_getbyname(root, &backing) == 0);
	} else {
		backing = NULL;
	}

	__assert(vfs_mount(backing, vfsroot, "/", rootfs, NULL) == 0);

	if (backing) {
		VOP_RELEASE(backing);
	}

	if (GET_KERNEL_ARGUMENT(initrd, bool))
		initrd_unpack();

	// spawn init

	proc_run_init();
	sched_threadexit();
}

cpu_t *get_bsp(void) {
	return &bsp_cpu;
}
