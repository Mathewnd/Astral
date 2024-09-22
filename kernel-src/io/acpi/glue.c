#include <uacpi/kernel_api.h>
#include <uacpi/status.h>
#include <uacpi/types.h>
#include <kernel/interrupt.h>
#include <kernel/scheduler.h>
#include <kernel/alloc.h>
#include <kernel/pmm.h>
#include <kernel/pci.h>
#include <kernel/timekeeper.h>
#include <arch/cpu.h>
#include <arch/mmu.h>
#include <arch/context.h>
#include <arch/io.h>
#include <logging.h>
#include <stdint.h>
#include <mutex.h>
#include <semaphore.h>
#include <spinlock.h>
#include <time.h>
#include <util.h>

void uacpi_kernel_log(uacpi_log_level lvl, const uacpi_char *str) {
	const char *lvlstr;

	switch (lvl) {
		case UACPI_LOG_DEBUG:
			lvlstr = "debug";
			break;
		case UACPI_LOG_TRACE:
			lvlstr = "trace";
			break;
		case UACPI_LOG_INFO:
			lvlstr = "info";
			break;
		case UACPI_LOG_WARN:
			lvlstr = "warn";
			break;
		case UACPI_LOG_ERROR:
			lvlstr = "error";
			break;
		default:
			lvlstr = "<invalid>";
	}

	printf("acpi: [%s] %s", lvlstr, str)
}

void *uacpi_kernel_alloc(uacpi_size size) {
	return alloc(size);
}

void *uacpi_kernel_calloc(uacpi_size count, uacpi_size size) {
	return alloc(count * size);
}

void uacpi_kernel_free(void *ptr) {
	if (ptr == NULL)
		return;

	return free(ptr);
}

void *uacpi_kernel_map(uacpi_phys_addr physical, uacpi_size length) {
	uintmax_t pageoffset = (uintptr_t)physical % PAGE_SIZE;
	void *virt = vmm_map(NULL, ROUND_UP(length + pageoffset, PAGE_SIZE), VMM_FLAGS_PHYSICAL,
		ARCH_MMU_FLAGS_READ | ARCH_MMU_FLAGS_WRITE | ARCH_MMU_FLAGS_NOEXEC,
		(void*)ROUND_DOWN(physical, PAGE_SIZE));

	__assert(virt);
	return (void *)((uintptr_t)virt + pageoffset);
}

void uacpi_kernel_unmap(void *ptr, uacpi_size length) {
	uintmax_t pageoffset = (uintptr_t)ptr % PAGE_SIZE;

	uintptr_t addr = (uintptr_t)ptr;
	vmm_unmap((void*)ROUND_DOWN(addr, PAGE_SIZE), ROUND_UP(length + pageoffset, PAGE_SIZE), 0);
}

uacpi_status uacpi_kernel_raw_memory_read(uacpi_phys_addr address, uacpi_u8 width, uacpi_u64 *out) {
	void *ptr = uacpi_kernel_map(address, width);

	switch (width) {
		case 1:
			*out = *(volatile uint8_t *)ptr;
			break;
		case 2:
			*out = *(volatile uint16_t *)ptr;
			break;
		case 4:
			*out = *(volatile uint32_t *)ptr;
			break;
		case 8:
			*out = *(volatile uint64_t *)ptr;
			break;
		default:
			uacpi_kernel_unmap(ptr, width);
			return UACPI_STATUS_INVALID_ARGUMENT;
	}

	uacpi_kernel_unmap(ptr, width);
	return UACPI_STATUS_OK;
}

uacpi_status uacpi_kernel_raw_memory_write(uacpi_phys_addr address, uacpi_u8 width, uacpi_u64 in) {
	void *ptr = uacpi_kernel_map(address, width);

	switch (width) {
		case 1:
			*(volatile uint8_t *)ptr = in;
			break;
		case 2:
			*(volatile uint16_t *)ptr = in;
			break;
		case 4:
			*(volatile uint32_t *)ptr = in;
			break;
		case 8:
			*(volatile uint64_t *)ptr = in;
			break;
		default:
			uacpi_kernel_unmap(ptr, width);
			return UACPI_STATUS_INVALID_ARGUMENT;
	}

	uacpi_kernel_unmap(ptr, width);
	return UACPI_STATUS_OK;
}

#ifdef __x86_64__
uacpi_status uacpi_kernel_raw_io_write(uacpi_io_addr addr, uacpi_u8 width, uacpi_u64 value) {
	switch (width) {
		case 1:
			outb(addr, value);
			break;
		case 2:
			outw(addr, value);
			break;
		case 4:
			outd(addr, value);
			break;
		default:
			return UACPI_STATUS_INVALID_ARGUMENT;
	}

	return UACPI_STATUS_OK;
}

uacpi_status uacpi_kernel_raw_io_read(uacpi_io_addr addr, uacpi_u8 width, uacpi_u64 *out) {
	switch (width) {
		case 1:
			*out = inb(addr);
			break;
		case 2:
			*out = inw(addr);
			break;
		case 4:
			*out = ind(addr);
			break;
		default:
			return UACPI_STATUS_INVALID_ARGUMENT;
	}

	return UACPI_STATUS_OK;
}

// Stolen from https://github.com/osdev0/cc-runtime/blob/dcdf5d82973e77edee597a047a3ef66300903de9/cc-runtime.c#L2229
int __popcountdi2(int64_t a) {
	uint64_t x2 = (uint64_t)a;
	x2 = x2 - ((x2 >> 1) & 0x5555555555555555uLL);
	// Every 2 bits holds the sum of every pair of bits (32)
	x2 = ((x2 >> 2) & 0x3333333333333333uLL) + (x2 & 0x3333333333333333uLL);
	// Every 4 bits holds the sum of every 4-set of bits (3 significant bits) (16)
	x2 = (x2 + (x2 >> 4)) & 0x0F0F0F0F0F0F0F0FuLL;
	// Every 8 bits holds the sum of every 8-set of bits (4 significant bits) (8)
	uint32_t x = (uint32_t)(x2 + (x2 >> 32));
	// The lower 32 bits hold four 16 bit sums (5 significant bits).
	//   Upper 32 bits are garbage
	x = x + (x >> 16);
	// The lower 16 bits hold two 32 bit sums (6 significant bits).
	//   Upper 16 bits are garbage
	return (x + (x >> 8)) & 0x0000007F; // (7 significant bits)
}
#else
uacpi_status uacpi_kernel_raw_io_read(uacpi_io_addr addr, uacpi_u8 width, uacpi_u64 *out) {
	return UACPI_STATUS_UNIMPLEMENTED;
}
uacpi_status uacpi_kernel_raw_io_write(uacpi_io_addr addr, uacpi_u8 width, uacpi_u64 value) {
	return UACPI_STATUS_UNIMPLEMENTED;
}
#endif

uacpi_status uacpi_kernel_io_map(uacpi_io_addr base, uacpi_size size, uacpi_handle *outhandle) {
	*outhandle = (uacpi_handle)base;
	return UACPI_STATUS_OK;
}

void uacpi_kernel_io_unmap(uacpi_handle handle) {
	(void)handle;
}

uacpi_status uacpi_kernel_io_read(uacpi_handle handle, uacpi_size offset, uacpi_u8 width, uacpi_u64 *out) {
	return uacpi_kernel_raw_io_read((uacpi_io_addr)handle + offset, width, out);
}

uacpi_status uacpi_kernel_io_write(uacpi_handle handle, uacpi_size offset, uacpi_u8 width, uacpi_u64 value) {
	return uacpi_kernel_raw_io_write((uacpi_io_addr)handle + offset, width, value);
}

uacpi_status uacpi_kernel_pci_read(uacpi_pci_address *address, uacpi_size offset, uacpi_u8 width, uacpi_u64 *out) {
	if (address->segment != 0) {
		printf("reading from PCI segment %u is not supported\n", address->segment);
		return UACPI_STATUS_UNIMPLEMENTED;
	}

	switch (width) {
		case 1: {
			*out = pci_read8(address->bus, address->device, address->function, offset);
			break;
		}
		case 2: {
			*out = pci_read16(address->bus, address->device, address->function, offset);
			break;
		}
		case 4: {
			*out = pci_read32(address->bus, address->device, address->function, offset);
			break;
		}
		default:
			return UACPI_STATUS_INVALID_ARGUMENT;
	}

	return UACPI_STATUS_OK;
}

uacpi_status uacpi_kernel_pci_write(uacpi_pci_address *address, uacpi_size offset, uacpi_u8 width, uacpi_u64 value) {
	if (address->segment != 0) {
		printf("writing to PCI segment %u is not supported\n", address->segment);
		return UACPI_STATUS_UNIMPLEMENTED;
	}

	switch (width) {
		case 1: {
			pci_write8(address->bus, address->device, address->function, offset, value);
			break;
		}
		case 2: {
			pci_write16(address->bus, address->device, address->function, offset, value);
			break;
		}
		case 4: {
			pci_write32(address->bus, address->device, address->function, offset, value);
			break;
		}
		default:
			return UACPI_STATUS_INVALID_ARGUMENT;
	}

	return UACPI_STATUS_OK;
}

uacpi_u64 uacpi_kernel_get_ticks(void) {
	// uACPI expects ticks in hundreds of nanoseconds
	return timespec_ns(timekeeper_timefromboot()) / 100;
}

void uacpi_kernel_stall(uacpi_u8 usec) {
	timespec_t start = timekeeper_timefromboot();
	while (timespec_diffus(start, timekeeper_timefromboot()) < usec) CPU_PAUSE();
}

void uacpi_kernel_sleep(uacpi_u64 msec) {
	sched_sleep_us(msec * 1000);
}

struct acpi_irqctx {
	uacpi_interrupt_handler handler;
	uacpi_handle ctx;
};

static void acpi_irq(isr_t *isr, context_t *ctx) {
	struct acpi_irqctx *actx = isr->priv;
	actx->handler(actx->ctx);
}

uacpi_status uacpi_kernel_install_interrupt_handler(uacpi_u32 irq, uacpi_interrupt_handler handler, uacpi_handle ctx, uacpi_handle *outhandle) {
	struct acpi_irqctx *actx = alloc(sizeof(struct acpi_irqctx));
	__assert(actx);

	isr_t *isr = interrupt_allocate(acpi_irq, arch_apic_eoi, IPL_ACPI);
	__assert(isr);

	actx->handler = handler;
	actx->ctx = ctx;
	isr->priv = actx;

	arch_ioapic_setirq(irq, isr->id & 0xff, current_cpu_id(), false);

	*outhandle = isr;
	return UACPI_STATUS_OK;
}

uacpi_status uacpi_kernel_uninstall_interrupt_handler(uacpi_interrupt_handler handler, uacpi_handle handle) {
	(void)handler; (void)handle;

	// We currently don't have an API to unregister interrupts
	printf("acpi: interrupt handler uninstallation is a TODO\n");
	return UACPI_STATUS_UNIMPLEMENTED;
}

struct acpi_work {
	uacpi_work_handler handler;
	uacpi_handle ctx;
	struct acpi_work *next;
};

struct acpi_workctx {
	semaphore_t sem;
	spinlock_t queuelock;
	thread_t *thread;
	struct acpi_work *head;
};

static struct acpi_workctx gpework;
static struct acpi_workctx notifywork;

static void acpi_initwork(struct acpi_workctx *ctx, void (*proc)(void)) {
	SEMAPHORE_INIT(&ctx->sem, 0);
	SPINLOCK_INIT(ctx->queuelock);

	ctx->thread = sched_newthread(proc, PAGE_SIZE * 4, 0, NULL, NULL);
	__assert(ctx->thread);
	sched_queue(ctx->thread);
}

static void acpi_dowork(struct acpi_workctx *ctx) {
	for (;;) {
		semaphore_wait(&ctx->sem, true);

		struct acpi_work *work = NULL;

		bool irqstate = spinlock_acquireirqclear(&ctx->queuelock);
		if (ctx->head) {
			work = ctx->head;
			ctx->head = work->next;
		}
		spinlock_releaseirqrestore(&ctx->queuelock, irqstate);

		if (work == NULL)
			continue;

		work->handler(work->ctx);
		free(work);
	}
}

static void acpi_dogpework() {
	sched_target_cpu(get_bsp());
	sched_yield();

	acpi_dowork(&gpework);
}

static void acpi_donotifywork() {
	acpi_dowork(&notifywork);
}

uacpi_status uacpi_kernel_initialize(uacpi_init_level lvl) {
	if (lvl != UACPI_INIT_LEVEL_SUBSYSTEM_INITIALIZED)
		return UACPI_STATUS_OK;

	acpi_initwork(&gpework, acpi_dogpework);
	acpi_initwork(&notifywork, acpi_donotifywork);

	return UACPI_STATUS_OK;
}

void uacpi_kernel_deinitialize() { }

uacpi_status uacpi_kernel_schedule_work(
	uacpi_work_type type, uacpi_work_handler handler, uacpi_handle ctx) {
	struct acpi_work *work;
	struct acpi_workctx *workctx;

	work = alloc(sizeof(*work));
	if (work == NULL)
		return UACPI_STATUS_OUT_OF_MEMORY;

	work->ctx = ctx;
	work->handler = handler;

	switch (type) {
		case UACPI_WORK_GPE_EXECUTION:
			workctx = &gpework;
			break;
		default:
			workctx = &notifywork;
			break;
	}

	bool irqstate = spinlock_acquireirqclear(&workctx->queuelock);
	work->next = workctx->head;
	workctx->head = work;
	spinlock_releaseirqrestore(&workctx->queuelock, irqstate);

	semaphore_signal(&workctx->sem);
	return UACPI_STATUS_OK;
}

static void work_await(struct acpi_workctx *ctx) {
	for (;;) {
		bool empty;

		bool irqstate = spinlock_acquireirqclear(&ctx->queuelock);
		empty = ctx->head == NULL;
		spinlock_releaseirqrestore(&ctx->queuelock, irqstate);

		if (empty)
			return;

		sched_sleep_us(100 * 1000);
	}
}

uacpi_status uacpi_kernel_wait_for_work_completion(void) {
	work_await(&gpework);
	work_await(&notifywork);
	return UACPI_STATUS_OK;
}

uacpi_status uacpi_kernel_handle_firmware_request(
	uacpi_firmware_request *req) {
	switch (req->type) {
		case UACPI_FIRMWARE_REQUEST_TYPE_BREAKPOINT:
			break;
		case UACPI_FIRMWARE_REQUEST_TYPE_FATAL:
			printf("acpi: fatal firmware error: type=%d code=%d arg=%d\n",
				req->fatal.type, req->fatal.code, req->fatal.arg)
			break;
	}

	return UACPI_STATUS_OK;
}

uacpi_handle uacpi_kernel_create_mutex(void) {
	mutex_t *mut = alloc(sizeof(mutex_t));
	if (mut == NULL)
		return mut;

	MUTEX_INIT(mut);
	return mut;
}

void uacpi_kernel_free_mutex(uacpi_handle mut) {
	uacpi_kernel_free(mut);
}

uacpi_bool uacpi_kernel_acquire_mutex(
	uacpi_handle mut, uacpi_u16 timeout) {
	if (timeout == 0xFFFF) {
		MUTEX_ACQUIRE(mut, true);
		return UACPI_TRUE;
	}

	return MUTEX_ACQUIRE_TIMED(mut, timeout * 1000, true);
}

void uacpi_kernel_release_mutex(uacpi_handle mut) {
	MUTEX_RELEASE(mut);
}

uacpi_handle uacpi_kernel_create_event(void) {
	semaphore_t *sem = alloc(sizeof(semaphore_t));
	if (sem == NULL)
		return sem;

	SEMAPHORE_INIT(sem, 1);
	return sem;
}

void uacpi_kernel_free_event(uacpi_handle sem) {
	uacpi_kernel_free(sem);
}

uacpi_bool uacpi_kernel_wait_for_event(
	uacpi_handle sem, uacpi_u16 timeout) {

	if (timeout == 0xFFFF) {
		semaphore_wait(sem, true);
		return UACPI_TRUE;
	}

	return semaphore_timedwait(sem, timeout * 1000, true);
}

void uacpi_kernel_signal_event(uacpi_handle sem) {
	semaphore_signal(sem);
}

void uacpi_kernel_reset_event(uacpi_handle sem) {
	semaphore_reset(sem);
}

uacpi_thread_id uacpi_kernel_get_thread_id(void) {
	return current_thread();
}

uacpi_handle uacpi_kernel_create_spinlock(void) {
	spinlock_t *lock = alloc(sizeof(spinlock_t));
	if (lock == NULL)
		return lock;

	SPINLOCK_INIT(*lock);
	return lock;
}

void uacpi_kernel_free_spinlock(uacpi_handle lock) {
	uacpi_kernel_free(lock);
}

uacpi_cpu_flags uacpi_kernel_spinlock_lock(uacpi_handle lock) {
	return spinlock_acquireirqclear(lock);
}

void uacpi_kernel_spinlock_unlock(uacpi_handle lock, uacpi_cpu_flags intstate) {
	spinlock_releaseirqrestore(lock, intstate);
}
