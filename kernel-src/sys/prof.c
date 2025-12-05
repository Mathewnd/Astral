#include <kernel/init.h>
#include <kernel/devfs.h>
#include <mutex.h>
#include <ringbuffer.h>
#include <semaphore.h>
#include <kernel/thread.h>
#include <kernel/scheduler.h>
#include <kernel/vfs.h>
#include <arch/smp.h>
#include <logging.h>

#ifdef ENABLE_PROFILING

#define PROFILING_GLOBAL_BUFFER_SIZE (256 * 1024)
#define PROFILING_PRIVATE_BUFFER_SIZE (256 * 1024) // the size of each cpu's private ring buffer

static ringbuffer_t global_ringbuffer;
static MUTEX_DEFINE(mutex);
static pollheader_t pollheader;
static semaphore_t wait_sem;
static int remaining;

// called from a possibly NMI context!
void profiling_insert(uint8_t size, void *data) {
	size_t write_size = sizeof(uint8_t) + sizeof(uintptr_t) * size;
	if (current_cpu()->prof_ringbuffer && RINGBUFFER_FREESPACE(current_cpu()->prof_ringbuffer) >= write_size) {
		size_t written = ringbuffer_write(current_cpu()->prof_ringbuffer, data, write_size);
		if (write_size != written) for (;;);
	}
}

static void profiling_thread() {
	// this will be shared between the cpu and this thread
	ringbuffer_t ringbuffer;
	cpu_t *cpu = current_thread()->kernelarg;
	__assert(ringbuffer_init(&ringbuffer, PROFILING_PRIVATE_BUFFER_SIZE) == 0);
	sched_target_cpu(cpu);
	cpu->prof_ringbuffer = &ringbuffer;

	if (__atomic_sub_fetch(&remaining, 1, __ATOMIC_SEQ_CST) == 0)
		semaphore_signal(&wait_sem);

	for (;;) {
		// profiling uses a NMI, which means that the handler cannot call into the scheduler
		// without the possibility of a deadlock. for this reason, this thread will sleep for a set
		// amount of time and only wake up to insert the data into a more reasonable place.
		// by default, sleep for 10 ms
		sched_sleep_us(10000);

		MUTEX_ACQUIRE(&mutex);

		int read = 0;

		for (;;) {
			uint8_t data[1 + sizeof(uintptr_t) * 256];
			size_t read_count = ringbuffer_read(&ringbuffer, data, 1);
			if (read_count == 0)
				break;

			read_count = ringbuffer_read(&ringbuffer, data + 1, data[0] * sizeof(uintptr_t));
			__assert(read_count == data[0] * sizeof(uintptr_t));
			size_t write_size = 1 + data[0] * sizeof(uintptr_t);

			if (RINGBUFFER_FREESPACE(&global_ringbuffer) >= write_size) {
				size_t written = ringbuffer_write(&global_ringbuffer, data, write_size);
				__assert(write_size == written);
				read = 1;
			}

		}

		if (read)
			poll_event(&pollheader, POLLIN);

		MUTEX_RELEASE(&mutex);
	}
}

static int internal_poll(polldata_t *data, int events) {
	int revents = 0;

	if (events & POLLOUT)
		revents |= POLLERR;

	if ((events & POLLIN) && RINGBUFFER_DATACOUNT(&global_ringbuffer))
		revents |= POLLIN;

	if (revents == 0 && data)
		poll_add(&pollheader, data, events);

	return revents;
}

static int device_poll(int minor, polldata_t *data, int events) {
	(void)minor;
	MUTEX_ACQUIRE(&mutex);

	int revents = internal_poll(data, events);

	MUTEX_RELEASE(&mutex);

	return revents;
}

static int device_read(int minor, iovec_iterator_t *iovec_iterator, size_t size, uintmax_t offset, int flags, size_t *readc) {
	(void)minor;
	if (unlikely(size == 0)) {
		*readc = 0;
		return 0;
	}

	polldesc_t desc;
	int error = poll_initdesc(&desc, 1);
	if (error)
		return error;

	MUTEX_ACQUIRE(&mutex);

	for (;;) {
		int revents = internal_poll(&desc.data[0], POLLIN);
		if (revents)
			break;

		if (flags & V_FFLAGS_NONBLOCKING) {
			error = EAGAIN;
			goto leave;
		}

		MUTEX_RELEASE(&mutex);
		error = poll_dowait(&desc, 0);

		if (error)
			goto leave_nolock;
		poll_leave(&desc);
		MUTEX_ACQUIRE(&mutex);
	}

	*readc = iovec_iterator_read_from_ringbuffer(iovec_iterator, &global_ringbuffer, size);
	if (*readc == RINGBUFFER_USER_COPY_FAILED)
		error = EFAULT;

	__assert(*readc);

	leave:
	MUTEX_RELEASE(&mutex);
	leave_nolock:
	poll_leave(&desc);
	poll_destroydesc(&desc);
	return error;
}

static int device_ioctl(int, unsigned long request, void *, int *result, cred_t *cred) {
	if (request != 12345678)
		return ENOTTY;

	// only supported ioctl is a truncation one: remove all previous data

	MUTEX_ACQUIRE(&mutex);

	ringbuffer_truncate(&global_ringbuffer, PROFILING_GLOBAL_BUFFER_SIZE);

	MUTEX_RELEASE(&mutex);

	*result = 0;
	return 0;
}

static devops_t devops = {
	.read = device_read,
	.poll = device_poll,
	.ioctl = device_ioctl
};

static void profiling_init() {
	// initialize global profiling data
	POLL_INITHEADER(&pollheader);
	__assert(ringbuffer_init(&global_ringbuffer, PROFILING_GLOBAL_BUFFER_SIZE) == 0);
	__assert(devfs_register(&devops, "prof", V_TYPE_CHDEV, DEV_MAJOR_PROF, 0, 0400, NULL) == 0);

	// start a thread for each cpu
	SEMAPHORE_INIT(&wait_sem, 0);
	remaining = arch_smp_cpusawake;

	for (int i = 0; i < arch_smp_cpusawake; ++i) {
		thread_t *thread = sched_newthread(profiling_thread, PAGE_SIZE * 2, -100, NULL, NULL);
		__assert(thread);

		thread->kernelarg = smp_cpus[i];
		sched_queue(thread);
	}

	semaphore_wait(&wait_sem, false);
}

INIT_ROUTINE_DEFINE(profiling, INIT_ROUTINE_FLAGS_NONE, profiling_init, devfs, smp);

#endif
