#include <arch/ldt.h>
#include <arch/gdt.h>
#include <kernel/alloc.h>
#include <errno.h>
#include <arch/cpu.h>
#include <arch/smp.h>
#include <spinlock.h>

static spinlock_t ldt_invalidate_lock;
static size_t done;

void arch_ldt_invalidate(void) {
	arch_gdt_set_ldt(current_thread()->proc->ldt, 0xffff);
	__atomic_add_fetch(&done, 1, __ATOMIC_SEQ_CST);
}

int arch_ldt_set_entry(int which, ldt_entry_t entry) {
	// is an ldt already loaded?
	if (__atomic_load_n(&current_thread()->proc->ldt, __ATOMIC_RELAXED) == NULL) {
		// allocate one and atomically set it
		ldt_entry_t *ldt = alloc(sizeof(ldt_entry_t) * 8192);
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
				// reading the vmm context like this is racey but there are no ill side effects other than spurious shootdowns
				// if a cpu changes to this vmmctx, they will already have done a ldt invalidation.
				// if it changes out of it, same thing
				if (smp_cpus[i] == current_cpu() || (smp_cpus[i]->vmmctx != current_thread()->vmmctx))
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
	*dst = alloc(sizeof(ldt_entry_t) * 8192);
	if (!*dst)
		return ENOMEM;

	memcpy(*dst, src, sizeof(ldt_entry_t) * 8192);
	return 0;
}
