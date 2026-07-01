#include <arch/ldt.h>
#include <arch/gdt.h>
#include <kernel/alloc.h>
#include <errno.h>
#include <arch/cpu.h>
#include <arch/smp.h>
#include <spinlock.h>

static spinlock_t ldt_invalidate_lock;
static size_t done;

#define LDT_ENTRIES 8192

#define LDT_ENTRY_ACCESS_BYTE(entry) (((entry) >> 40) & 0xff)
#define LDT_ENTRY_FLAGS(entry) (((entry) >> 52) & 0xf)

#define LDT_ENTRY_CODE 0x08
#define LDT_ENTRY_S 0x10
#define LDT_ENTRY_DPL_MASK 0x60
#define LDT_ENTRY_DPL_USER 0x60
#define LDT_ENTRY_PRESENT 0x80

#define LDT_ENTRY_LONG 0x2
#define LDT_ENTRY_DB 0x4

void arch_ldt_invalidate(void) {
	arch_gdt_set_ldt(current_thread()->proc->ldt, 0xffff);
	__atomic_add_fetch(&done, 1, __ATOMIC_SEQ_CST);
}

int arch_ldt_set_entry(unsigned int which, ldt_entry_t entry) {
	if (which >= LDT_ENTRIES)
		return EINVAL;

	if (entry != 0) {
		uint8_t access = LDT_ENTRY_ACCESS_BYTE(entry);
		uint8_t flags = LDT_ENTRY_FLAGS(entry);

		if ((access & LDT_ENTRY_S) == 0)
			return EINVAL;

		if ((access & LDT_ENTRY_DPL_MASK) != LDT_ENTRY_DPL_USER)
			return EINVAL;

		if ((access & LDT_ENTRY_PRESENT) == 0)
			return EINVAL;

		if ((access & LDT_ENTRY_CODE) == 0) {
			if (flags & LDT_ENTRY_LONG)
				return EINVAL;
		} else if ((flags & (LDT_ENTRY_LONG | LDT_ENTRY_DB)) == (LDT_ENTRY_LONG | LDT_ENTRY_DB)) {
			return EINVAL;
		}
	}

	if (__atomic_load_n(&current_thread()->proc->ldt, __ATOMIC_RELAXED) == NULL) {
		ldt_entry_t *ldt = alloc(sizeof(ldt_entry_t) * LDT_ENTRIES);
		if (ldt == NULL)
			return ENOMEM;

		ldt_entry_t *expected = NULL;

		bool swap_ok = __atomic_compare_exchange_n(&current_thread()->proc->ldt, &expected, ldt, false, __ATOMIC_SEQ_CST, __ATOMIC_RELAXED);
		if (!swap_ok)
			free(ldt);

		bool do_shootdown = // do shootdown if
		   	swap_ok // we actually need to
			&& arch_smp_cpusawake >= 2 // there are multiple cpus in the system
			&& current_thread()->proc->runningthreadcount > 1; // in a process which has multiple threads running

		if (do_shootdown) {
			long old_ipl = spinlock_acquire_raise_ipl(&ldt_invalidate_lock, IPL_DPC);
			size_t shootdown_total = 1;
			done = 0;

			for (int i = 0; i < arch_smp_cpusawake; ++i) {
				// reading the mm context like this is racey but there are no ill side effects other than spurious shootdowns
				// if a cpu changes to this mmctx, they will already have done a ldt invalidation.
				// if it changes out of it, same thing
				if (smp_cpus[i] == current_cpu() || (smp_cpus[i]->mmctx != current_thread()->mmctx))
					continue;

				++shootdown_total;
				arch_smp_send_ipi(smp_cpus[i], &smp_cpus[i]->isr[0xfc], ARCH_SMP_IPI_TARGET, false);
			}

			arch_ldt_invalidate();

			while (__atomic_load_n(&done, __ATOMIC_SEQ_CST) != shootdown_total) asm("pause");

			spinlock_release_lower_ipl(&ldt_invalidate_lock, old_ipl);
		}
	}

	__atomic_store_n(&current_thread()->proc->ldt[which], entry, __ATOMIC_RELAXED);

	return 0;
}

int arch_ldt_fork(ldt_entry_t **dst, ldt_entry_t *src) {
	*dst = alloc(sizeof(ldt_entry_t) * LDT_ENTRIES);
	if (*dst == NULL)
		return ENOMEM;

	memcpy(*dst, src, sizeof(ldt_entry_t) * LDT_ENTRIES);
	return 0;
}
