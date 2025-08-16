#include <arch/cpu.h>
#include <kernel/init.h>
#include <logging.h>
#include <kernel/cmdline.h>
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

void kernel_entry() {
	cpu_set(&bsp_cpu);
	logging_sethook(arch_early_log);

	init_run_routine(INIT_GET_ROUTINE(bsp_early));

	cmdline_parse();

	init_run_all_routines();

	// kernel is fully up and running by now
	// mount root filesystem

	char *root   = cmdline_get("root");
	char *rootfs = cmdline_get("rootfs");

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

	if (cmdline_get("initrd"))
		initrd_unpack();

	// spawn init

	proc_run_init();
	sched_threadexit();
}

cpu_t *get_bsp(void) {
	return &bsp_cpu;
}
