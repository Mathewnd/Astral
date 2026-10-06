#ifdef KCOV_ENABLED
#include <kernel/kcov.h>
#include <kernel/alloc.h>
#include <kernel/devfs.h>
#include <kernel/init.h>
#include <kernel/usercopy.h>
#include <arch/cpu.h>
#include <logging.h>

#define KCOV_MAX 256

static MUTEX_DEFINE(map_mutex);
static kcov_t *kcov_map[KCOV_MAX];

static int allocate_kcov(int *out) {
	kcov_t *kcov = alloc(sizeof(kcov_t));
	if (kcov == NULL)
		return ENOMEM;

	MUTEX_INIT(&kcov->mapping_mutex);
	__atomic_store_n(&kcov->refcount, 1, __ATOMIC_RELAXED);

	MUTEX_ACQUIRE(&map_mutex);
	int minor;
	for (minor = 0; minor < KCOV_MAX; ++minor) {
		if (kcov_map[minor] == NULL) {
			kcov_map[minor] = kcov;
			break;
		}
	}

	if (minor < KCOV_MAX)
		kcov->minor = minor;

	MUTEX_RELEASE(&map_mutex);

	if (minor == KCOV_MAX) {
		free(kcov);
		return EAGAIN;
	}

	*out = minor;
	return 0;
}

static void free_kcov(int minor) {
	MUTEX_ACQUIRE(&map_mutex);
	kcov_t *kcov = kcov_map[minor];
	kcov_map[minor] = NULL;
	MUTEX_RELEASE(&map_mutex);

	__assert(kcov);
	free(kcov);
}

void kcov_hold(kcov_t *kcov) {
	__atomic_add_fetch(&kcov->refcount, 1, __ATOMIC_RELAXED);
}

void kcov_release(kcov_t *kcov) {
	if (__atomic_sub_fetch(&kcov->refcount, 1, __ATOMIC_RELAXED) == 0) {
		__atomic_thread_fence(__ATOMIC_ACQ_REL);
		// this is safe to do as we are the only ones holding a reference
		kcov_free_buffer(kcov);
		free_kcov(kcov->minor);
	}
}

static kcov_t *get_kcov(int minor) {
	if (minor >= KCOV_MAX)
		return NULL;

	MUTEX_ACQUIRE(&map_mutex);
	kcov_t *kcov = kcov_map[minor];
	MUTEX_RELEASE(&map_mutex);

	return kcov;
}

static devops_t ops;

static int create_node_for_kcov(kcov_t *kcov, int minor, vnode_t **vnodep) {
	vnode_t *vnode;
	int error = devfs_register_anonymous(&ops, V_TYPE_CHDEV, DEV_MAJOR_KCOV, minor,
					     0, NULL, &vnode);
	if (error)
		return error;

	kcov->vnode_handle = vnode;
	devfs_remove_anonymous(DEV_MAJOR_KCOV, minor);

	// a kcov will have two references initially:
	// - one for the thread holding it
	// - one for the vnode
	kcov_hold(kcov);
	*vnodep = vnode;
	return 0;
}

static int open(int oldminor, vnode_t **vnodep, int flags) {
	vnode_t *old = *vnodep;

	if (oldminor != KCOV_MAX)
		return ENODEV;

	// if the thread already has a kcov attached, return it
	if (current_thread()->kcov) {
		kcov_t *kcov = current_thread()->kcov;

		int error = create_node_for_kcov(kcov, kcov->minor, vnodep);
		if (error)
			return error;

		VOP_RELEASE(old);
		return 0;
	}

	int minor;
	int error = allocate_kcov(&minor);
	if (error)
		return error;

	kcov_t *kcov = get_kcov(minor);
	error = create_node_for_kcov(kcov, minor, vnodep);
	if (error) {
		kcov_release(kcov);
		return error;
	}

	current_thread()->kcov = kcov;
	VOP_RELEASE(old);
	return 0;
}

#define IOCTL_REQUEST_START 0xF1177000
#define IOCTL_REQUEST_STOP 0xF1177100
#define IOCTL_REQUEST_SET_SIZE 0xF1177200

static int ioctl(int minor, unsigned long request, void *arg, int *result, cred_t *cred) {
	kcov_t *kcov = current_thread()->kcov;
	if (kcov == NULL || kcov->minor != minor)
		return EINVAL;

	switch (request) {
		case IOCTL_REQUEST_SET_SIZE: {
			int size;
			int error = USERCOPY_POSSIBLY_FROM_USER(&size, arg, sizeof(size));
			if (error)
				return error;

			return kcov_set_buffer_size(size);
		}
		case IOCTL_REQUEST_START: {
			if (kcov->mode != KCOV_MODE_DISABLED)
				return EBUSY;

			if (kcov->buffer == NULL)
				return EINVAL;

			int mode;
			int error = USERCOPY_POSSIBLY_FROM_USER(&mode, arg, sizeof(mode));
			if (error)
				return error;

			return kcov_set_mode(mode);
		}
		case IOCTL_REQUEST_STOP:
			return kcov_set_mode(KCOV_MODE_DISABLED);
		default:
			return ENOTTY;
	}
}

static int mmap(int minor, void *addr, uintmax_t offset, int flags) {
	kcov_t *kcov = get_kcov(minor);
	__assert(kcov); // mapped kcov that doesn't exist...?

	if (offset % PAGE_SIZE)
		return EINVAL;

	// why would anyone do this?
	if ((flags & V_FFLAGS_SHARED) == 0)
		return EINVAL;

	MUTEX_ACQUIRE(&kcov->mapping_mutex);
	int error = EINVAL;
	if (kcov->buffer == NULL)
		goto cleanup;

	size_t start_entry = offset / sizeof(uint64_t);
	if (kcov->entry_count < start_entry)
		goto cleanup;

	void *phys = arch_mmu_getphysical(current_mm_context()->pagetable,
					  &kcov->buffer[start_entry]);
	__assert(phys);
	// this code is called holding the mm lock, so this is the way of doing this
	if (arch_mmu_map(current_mm_context()->pagetable, phys, addr,
			 mm_vnode_flags_to_mmu_flags(flags)) == false) {
		error = ENOMEM;
		goto cleanup;
	}

	++kcov->mappings;
	error = 0;
	cleanup:
	MUTEX_RELEASE(&kcov->mapping_mutex);
	return error;
}

static int munmap(int minor, void *addr, uintmax_t offset, int flags) {
	kcov_t *kcov = get_kcov(minor);
	__assert(kcov); // mapped kcov that doesn't exist...?

	if (offset % PAGE_SIZE)
		return EINVAL;

	// why would anyone do this?
	if ((flags & V_FFLAGS_SHARED) == 0)
		return EINVAL;

	// this code is called holding the mm lock, so this is the way of doing this
	arch_mmu_unmap(current_mm_context()->pagetable, addr);
	arch_mmu_invalidate_range(addr, PAGE_SIZE);

	MUTEX_ACQUIRE(&kcov->mapping_mutex);
	--kcov->mappings;
	MUTEX_RELEASE(&kcov->mapping_mutex);

	return 0;
}

static void inactive(int minor) {
	kcov_t *kcov = get_kcov(minor);
	if (kcov == NULL)
		return;

	kcov_release(kcov);
}

static devops_t ops = {
	.open = open,
	.inactive = inactive,
	.mmap = mmap,
	.munmap = munmap,
	.ioctl = ioctl
};

static void kcov_init(void) {
	__assert(devfs_register(&ops, "kcov", V_TYPE_CHDEV, DEV_MAJOR_KCOV, KCOV_MAX, 0600,
				NULL) == 0);
}

INIT_ROUTINE_DEFINE(kcov, INIT_ROUTINE_FLAGS_NONE, kcov_init, devfs);
#endif
