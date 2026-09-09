#include <uacpi/kernel_api.h>
#include <uacpi/status.h>
#include <uacpi/types.h>
#include <kernel/interrupt.h>
#include <kernel/init.h>
#include <kernel/scheduler.h>
#include <kernel/alloc.h>
#include <kernel/page.h>
#include <kernel/pci.h>
#include <kernel/timekeeper.h>
#include <kernel/work.h>
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

void *uacpi_kernel_alloc_zeroed(uacpi_size size) {
	return alloc(size);
}

void uacpi_kernel_free(void *ptr) {
	if (ptr == NULL)
		return;

	return free(ptr);
}

void *uacpi_kernel_map(uacpi_phys_addr physical, uacpi_size length) {
	uintmax_t pageoffset = (uintptr_t)physical % PAGE_SIZE;
	void *virt = mm_map(NULL, ROUND_UP(length + pageoffset, PAGE_SIZE), MM_RANGE_FLAGS_PHYSICAL,
		ARCH_MMU_FLAGS_READ | ARCH_MMU_FLAGS_WRITE | ARCH_MMU_FLAGS_NOEXEC,
		(void*)ROUND_DOWN(physical, PAGE_SIZE));

	__assert(virt);
	return (void *)((uintptr_t)virt + pageoffset);
}

void uacpi_kernel_unmap(void *ptr, uacpi_size length) {
	uintmax_t pageoffset = (uintptr_t)ptr % PAGE_SIZE;

	uintptr_t addr = (uintptr_t)ptr;
	mm_unmap((void*)ROUND_DOWN(addr, PAGE_SIZE), ROUND_UP(length + pageoffset, PAGE_SIZE), 0);
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
uacpi_status uacpi_kernel_io_read8(uacpi_handle handle, uacpi_size offset, uacpi_u8 *out) {
	*out = inb((uacpi_io_addr)handle + offset);
	return UACPI_STATUS_OK;
}

uacpi_status uacpi_kernel_io_read16(uacpi_handle handle, uacpi_size offset, uacpi_u16 *out) {
	*out = inw((uacpi_io_addr)handle + offset);
	return UACPI_STATUS_OK;
}

uacpi_status uacpi_kernel_io_read32(uacpi_handle handle, uacpi_size offset, uacpi_u32 *out) {
	*out = ind((uacpi_io_addr)handle + offset);
	return UACPI_STATUS_OK;
}

uacpi_status uacpi_kernel_io_write8(uacpi_handle handle, uacpi_size offset, uacpi_u8 v) {
	outb((uacpi_io_addr)handle + offset, v);
	return UACPI_STATUS_OK;
}

uacpi_status uacpi_kernel_io_write16(uacpi_handle handle, uacpi_size offset, uacpi_u16 v) {
	outw((uacpi_io_addr)handle + offset, v);
	return UACPI_STATUS_OK;
}

uacpi_status uacpi_kernel_io_write32(uacpi_handle handle, uacpi_size offset, uacpi_u32 v) {
	outd((uacpi_io_addr)handle + offset, v);
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

uacpi_status uacpi_kernel_pci_device_open(
    uacpi_pci_address address, uacpi_handle *out_handle
) {
	uint64_t v = ((uint64_t)address.segment << 48) | ((uint64_t)address.bus << 32) | ((uint64_t)address.device << 16) | ((uint64_t)address.function);
	*out_handle = (uacpi_handle)v;
	return UACPI_STATUS_OK;
}

void uacpi_kernel_pci_device_close(uacpi_handle handle) {
	(void)handle;
}

uacpi_status uacpi_kernel_pci_read32(uacpi_handle _handle, uacpi_size offset, uacpi_u32 *value) {
	uint64_t handle = (uint64_t)_handle;
	int segment = (handle >> 48) & 0xffff;
	int bus = handle >> 32;
	int device = (handle >> 16) & 0xffff;
	int function = handle & 0xffff;

	if (segment != 0) {
		printf("reading from PCI segment %u is not supported\n", segment);
		return UACPI_STATUS_UNIMPLEMENTED;
	}

	*value = pci_read32(bus, device, function, offset);
	return UACPI_STATUS_OK;
}

uacpi_status uacpi_kernel_pci_read16(uacpi_handle _handle, uacpi_size offset, uacpi_u16 *value) {
	uint64_t handle = (uint64_t)_handle;
	int segment = (handle >> 48) & 0xffff;
	int bus = handle >> 32;
	int device = (handle >> 16) & 0xffff;
	int function = handle & 0xffff;

	if (segment != 0) {
		printf("reading from PCI segment %u is not supported\n", segment);
		return UACPI_STATUS_UNIMPLEMENTED;
	}

	*value = pci_read16(bus, device, function, offset);
	return UACPI_STATUS_OK;
}

uacpi_status uacpi_kernel_pci_read8(uacpi_handle _handle, uacpi_size offset, uacpi_u8 *value) {
	uint64_t handle = (uint64_t)_handle;
	int segment = (handle >> 48) & 0xffff;
	int bus = handle >> 32;
	int device = (handle >> 16) & 0xffff;
	int function = handle & 0xffff;

	if (segment != 0) {
		printf("reading from PCI segment %u is not supported\n", segment);
		return UACPI_STATUS_UNIMPLEMENTED;
	}

	*value = pci_read8(bus, device, function, offset);
	return UACPI_STATUS_OK;
}

uacpi_status uacpi_kernel_pci_write32(uacpi_handle _handle, uacpi_size offset, uacpi_u32 value) {
	uint64_t handle = (uint64_t)_handle;
	int segment = (handle >> 48) & 0xffff;
	int bus = handle >> 32;
	int device = (handle >> 16) & 0xffff;
	int function = handle & 0xffff;

	if (segment != 0) {
		printf("reading from PCI segment %u is not supported\n", segment);
		return UACPI_STATUS_UNIMPLEMENTED;
	}

	pci_write32(bus, device, function, offset, value);
	return UACPI_STATUS_OK;
}

uacpi_status uacpi_kernel_pci_write16(uacpi_handle _handle, uacpi_size offset, uacpi_u16 value) {
	uint64_t handle = (uint64_t)_handle;
	int segment = (handle >> 48) & 0xffff;
	int bus = handle >> 32;
	int device = (handle >> 16) & 0xffff;
	int function = handle & 0xffff;

	if (segment != 0) {
		printf("reading from PCI segment %u is not supported\n", segment);
		return UACPI_STATUS_UNIMPLEMENTED;
	}

	pci_write16(bus, device, function, offset, value);
	return UACPI_STATUS_OK;
}

uacpi_status uacpi_kernel_pci_write8(uacpi_handle _handle, uacpi_size offset, uacpi_u8 value) {
	uint64_t handle = (uint64_t)_handle;
	int segment = (handle >> 48) & 0xffff;
	int bus = handle >> 32;
	int device = (handle >> 16) & 0xffff;
	int function = handle & 0xffff;

	if (segment != 0) {
		printf("reading from PCI segment %u is not supported\n", segment);
		return UACPI_STATUS_UNIMPLEMENTED;
	}

	pci_write8(bus, device, function, offset, value);
	return UACPI_STATUS_OK;
}

uacpi_u64 uacpi_kernel_get_nanoseconds_since_boot(void) {
	return timespec_ns(timekeeper_timefromboot());
}

void uacpi_kernel_stall(uacpi_u8 usec) {
	timekeeper_wait_us(usec);
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

#define ACPI_WORK_POOL_SIZE 16

struct acpi_workctx;

struct acpi_work {
	work_t work;
	list_node_t pool_node;
	uacpi_work_handler handler;
	uacpi_handle ctx;
	struct acpi_workctx *workctx;
};

struct acpi_workctx {
	work_queue_t *queue;
	spinlock_t pool_lock;
	list_t free_list;
	struct acpi_work pool[ACPI_WORK_POOL_SIZE];
	bool target_bsp;
};

static struct acpi_workctx gpework;
static struct acpi_workctx notifywork;

static void acpi_dowork(void *context, size_t pending) {
	(void)pending;

	struct acpi_work *work = context;
	struct acpi_workctx *workctx = work->workctx;
	uacpi_work_handler handler = work->handler;
	uacpi_handle handler_ctx = work->ctx;

	// no need to untarget later, a workctx that takes this path 
	// will always run on the bsp
	if (workctx->target_bsp)
		sched_reschedule_on_cpu(get_bsp(), true);

	handler(handler_ctx);

	long old_ipl = spinlock_acquire_raise_ipl(&workctx->pool_lock, IPL_ACPI);
	list_push_back(&workctx->free_list, &work->pool_node);
	spinlock_release_lower_ipl(&workctx->pool_lock, old_ipl);
}

uacpi_status uacpi_kernel_initialize(uacpi_init_level lvl) {
	(void)lvl;
	return UACPI_STATUS_OK;
}

void uacpi_kernel_deinitialize() { }

uacpi_status uacpi_kernel_schedule_work(
	uacpi_work_type type, uacpi_work_handler handler, uacpi_handle ctx) {
	struct acpi_workctx *workctx;

	switch (type) {
		case UACPI_WORK_GPE_EXECUTION:
			workctx = &gpework;
			break;
		default:
			workctx = &notifywork;
			break;
	}

	long old_ipl = spinlock_acquire_raise_ipl(&workctx->pool_lock, IPL_ACPI);
	list_node_t *node = list_pop_front(&workctx->free_list);
	if (node == NULL) {
		spinlock_release_lower_ipl(&workctx->pool_lock, old_ipl);
		return UACPI_STATUS_OUT_OF_MEMORY;
	}

	struct acpi_work *work = container_of(node, struct acpi_work, pool_node);
	work->handler = handler;
	work->ctx = ctx;
	spinlock_release_lower_ipl(&workctx->pool_lock, old_ipl);

	work_enqueue(workctx->queue, &work->work);
	return UACPI_STATUS_OK;
}

uacpi_status uacpi_kernel_wait_for_work_completion(void) {
	work_drain(gpework.queue);
	work_drain(notifywork.queue);
	return UACPI_STATUS_OK;
}

static void acpi_initwork(struct acpi_workctx *workctx, const char *name, bool target_bsp) {
	SPINLOCK_INIT(workctx->pool_lock);
	list_init(&workctx->free_list);
	workctx->target_bsp = target_bsp;
	workctx->queue = work_queue_create(name, 1, IPL_ACPI);
	__assert(workctx->queue);

	for (size_t i = 0; i < ACPI_WORK_POOL_SIZE; ++i) {
		struct acpi_work *work = &workctx->pool[i];
		WORK_INIT(&work->work, acpi_dowork, work);
		work->workctx = workctx;
		list_push_back(&workctx->free_list, &work->pool_node);
	}
}

static void acpi_work_init(void) {
	acpi_initwork(&gpework, "acpi-gpe", true);
	acpi_initwork(&notifywork, "acpi-notify", false);
}

INIT_ROUTINE_DEFINE(acpi_work, INIT_ROUTINE_FLAGS_NONE, acpi_work_init, work_queue);

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

uacpi_status uacpi_kernel_acquire_mutex(
	uacpi_handle mut, uacpi_u16 timeout) {
	if (timeout == 0xFFFF) {
		MUTEX_ACQUIRE(mut);
		return UACPI_STATUS_OK;
	}

	for (int i = 0; i < timeout; ++i) {
		if (MUTEX_TRY(mut))
			return UACPI_STATUS_OK;

		sched_sleep_us(1000);
	}

	return UACPI_STATUS_TIMEOUT;
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

uacpi_cpu_flags uacpi_kernel_lock_spinlock(uacpi_handle lock) {
	return spinlock_acquire_irq_clear(lock);
}

void uacpi_kernel_unlock_spinlock(uacpi_handle lock, uacpi_cpu_flags intstate) {
	spinlock_release_irq_restore(lock, intstate);
}
