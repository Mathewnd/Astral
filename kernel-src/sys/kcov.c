#include <kernel/kcov.h>
#include <kernel/alloc.h>
#include <kernel/devfs.h>
#include <kernel/init.h>
#include <kernel/usercopy.h>
#include <arch/cpu.h>
#include <logging.h>

#ifdef KCOV_ENABLED

#define MAX_ENTRIES (1 << 24)

KCOV_DISABLED void kcov_publish_pc(uint64_t pc) {
	kcov_t *kcov = current_thread()->kcov;

	uint64_t entry = kcov->buffer[0];
	if (entry >= kcov->entry_count)
		return;

	kcov->buffer[entry + 1] = pc;
	kcov->buffer[0] = entry + 1;
	__atomic_thread_fence(__ATOMIC_RELEASE);
}

KCOV_DISABLED void kcov_publish_cmp(uint64_t pc, uint64_t v1, uint64_t v2, uint64_t flags) {
	kcov_t *kcov = current_thread()->kcov;

	uint64_t entry = kcov->buffer[0];
	if (entry >= kcov->entry_count / 4)
		return;

	kcov->buffer[entry * 4 + 1] = flags;
	kcov->buffer[entry * 4 + 2] = v1;
	kcov->buffer[entry * 4 + 3] = v2;
	kcov->buffer[entry * 4 + 4] = pc;
	kcov->buffer[0] = entry + 1;
	__atomic_thread_fence(__ATOMIC_RELEASE);
}

// expects the mutex to be held OR this to be the last reference
KCOV_DISABLED void kcov_free_buffer(kcov_t *kcov) {
	if (kcov->buffer == NULL)
		return;

	mm_unmap(kcov->buffer, sizeof(uint64_t) * (kcov->entry_count + 1), 0);
	kcov->entry_count = 0;
	kcov->buffer = NULL;
}

KCOV_DISABLED static int resize_buffer(kcov_t *kcov, unsigned int entry_count) {
	if (entry_count == 0) {
		kcov_free_buffer(kcov);
		return 0;
	}

	void *tmp = mm_map(NULL, sizeof(uint64_t) * (entry_count + 1), MM_RANGE_FLAGS_ALLOCATE, 
			   ARCH_MMU_FLAGS_READ | ARCH_MMU_FLAGS_WRITE | ARCH_MMU_FLAGS_NOEXEC, NULL);
	if (tmp == NULL)
		return ENOMEM;

	void *old = kcov->buffer;
	size_t old_entries = kcov->entry_count;

	kcov->buffer = tmp;
	kcov->entry_count = entry_count;

	mm_unmap(old, (old_entries + 1) * sizeof(uint64_t), 0);

	return 0;
}

KCOV_DISABLED int kcov_set_mode(kcov_t *kcov, unsigned int mode) {
	__assert(kcov);

	if (mode != KCOV_MODE_DISABLED &&
	    mode != KCOV_MODE_PC &&
	    mode != KCOV_MODE_CMP)
		return EINVAL;

	MUTEX_ACQUIRE(&kcov->mapping_mutex);
	int error = 0;
	if (mode == KCOV_MODE_DISABLED) {
		if (current_thread()->kcov != kcov) {
			error = EINVAL;
			goto cleanup;
		}

		current_thread()->kcov = NULL;
	} else {
		if (current_thread()->kcov || kcov->mode != KCOV_MODE_DISABLED) {
			error = EBUSY;
			goto cleanup;
		}

		if (kcov->buffer == NULL) {
			error = EINVAL;
			goto cleanup;
		}

		kcov_hold(kcov);
	}

	kcov->mode = mode;
	if (mode != KCOV_MODE_DISABLED)
		current_thread()->kcov = kcov;

	cleanup:
	MUTEX_RELEASE(&kcov->mapping_mutex);
	if (error == 0 && mode == KCOV_MODE_DISABLED)
		kcov_release(kcov);
	return error;
}

KCOV_DISABLED int kcov_set_buffer_size(kcov_t *kcov, unsigned int entry_count) {
	if (entry_count < 0 || entry_count > MAX_ENTRIES)
		return EINVAL;

	MUTEX_ACQUIRE(&kcov->mapping_mutex);
	int error = 0;
	if (kcov->mappings || kcov->mode != KCOV_MODE_DISABLED) {
		error = EBUSY;
		goto cleanup;
	}

	if (kcov->buffer) {
		error = resize_buffer(kcov, entry_count);
		goto cleanup;
	}

	if (entry_count == 0)
		goto cleanup;

	kcov->buffer = mm_map(NULL, sizeof(uint64_t) * (entry_count + 1), MM_RANGE_FLAGS_ALLOCATE, 
			      ARCH_MMU_FLAGS_READ | ARCH_MMU_FLAGS_WRITE | ARCH_MMU_FLAGS_NOEXEC, NULL);
	if (kcov->buffer == NULL) {
		error = ENOMEM;
		goto cleanup;
	}
	kcov->entry_count = entry_count;

	cleanup:
	MUTEX_RELEASE(&kcov->mapping_mutex);
	return error;
}

#endif
