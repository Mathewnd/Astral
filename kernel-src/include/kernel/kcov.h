#ifndef _KCOV_H
#define _KCOV_H

#include <stdint.h>
#include <stddef.h>
#include <mutex.h>

#ifdef KCOV_ENABLED
#define KCOV_DISABLED __attribute__((no_sanitize_coverage))
#else
#define KCOV_DISABLED
#endif

#define KCOV_MODE_DISABLED 0
#define KCOV_MODE_PC 1
#define KCOV_MODE_CMP 2

#define KCOV_CMP_FLAGS_CONST 1
#define KCOV_CMP_FLAGS_8BIT 0
#define KCOV_CMP_FLAGS_16BIT 2
#define KCOV_CMP_FLAGS_32BIT 4
#define KCOV_CMP_FLAGS_64BIT 6

typedef struct {
	unsigned int mode; // transitions protected by mutex
	mutex_t mapping_mutex;
	unsigned int entry_count; // protected by mutex
	uint64_t *buffer; // pointer protected by mutex
	size_t mappings; // protected by mutex
	unsigned int refcount;
	unsigned int minor;
	void *vnode_handle;
} kcov_t;

void kcov_publish_pc(uint64_t pc);
void kcov_publish_cmp(uint64_t pc, uint64_t v1, uint64_t v2, uint64_t flags);
int kcov_set_buffer_size(kcov_t *kcov, unsigned int entry_count);
int kcov_set_mode(kcov_t *kcov, unsigned int mode);
void kcov_hold(kcov_t *kcov);
void kcov_release(kcov_t *kcov);
void kcov_free_buffer(kcov_t *kcov);

#endif
