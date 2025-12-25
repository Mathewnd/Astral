#include <kernel/init.h>
#include <uacpi/utilities.h>
#include <uacpi/resources.h>
#include <kernel/alloc.h>
#include <arch/cpu.h>
#include <logging.h>
#include <kernel/thread.h>
#include <kernel/scheduler.h>
#include <kernel/tty.h>
#include <arch/io.h>

#ifdef __x86_64__
#include <arch/apic.h>
#endif

#define PORT_RX 0
#define PORT_TX 0
#define PORT_INTERRUPT_ENABLE 1
#define PORT_DIVISOR_LOW 0
#define PORT_DIVISOR_HIGH 1
#define PORT_INTERRUPT_ID 2
#define PORT_FIFO_CONTROL 2
#define PORT_LINE_CONTROL 3
#define PORT_MODEM_CONTROL 4
#define PORT_LINE_STATUS 5
#define PORT_MODEM_STATUS 6

#define LINE_STATUS_DATA_READY 1
#define LINE_STATUS_TX_BUFFER_EMPTY 32

#define MODEM_CONTROL_DATA_TERMINAL_READY 1
#define MODEM_CONTROL_REQUEST_TO_SEND 2
#define MODEM_CONTROL_AUX_1 4
#define MODEM_CONTROL_AUX_2 8

#define INTERRUPT_STATUS_TX_EMPTY 2

#define INTERRUPT_ENABLE_NONE 0
#define INTERRUPT_ENABLE_DATA_AVAILABLE 1
#define INTERRUPT_ENABLE_TX_EMPTY 2

#define LINE_CONTROL_DIVISOR_LATCH 0x80
#define LINE_CONTROL_ENABLE_BREAK 0x40
#define LINE_CONTROL_PARITY_SPACE 0x38
#define LINE_CONTROL_PARITY_MARK 0x28
#define LINE_CONTROL_PARITY_EVEN 0x18
#define LINE_CONTROL_PARITY_ODD 8
#define LINE_CONTROL_STOP_BITS 4
#define LINE_CONTROL_WORD_LENGTH_8_BITS 3
#define LINE_CONTROL_WORD_LENGTH_7_BITS 2
#define LINE_CONTROL_WORD_LENGTH_6_BITS 1

#define BASE_BAUD 115200

// TODO make a proper serial.c to abstract most serial ports out.
// for now, only pc com will be implemented
// TODO FIFO support

typedef struct {
	int id;
	int io_base;
	uacpi_resource_irq irq;

	ringbuffer_t rx_ringbuffer;
	thread_t *handler_thread;
	semaphore_t rx_semaphore;

	tty_t *tty;

	int current_divisor;
} pc_com_t;

static inline void write_port(pc_com_t *pc_com, int port, int value) {
	outb(pc_com->io_base + port, value);
}

static inline int read_port(pc_com_t *pc_com, int port) {
	return inb(pc_com->io_base + port);
}

static bool has_data(pc_com_t *pc_com) {
	return read_port(pc_com, PORT_LINE_STATUS) & LINE_STATUS_DATA_READY;
}

static char read_data(pc_com_t *pc_com) {
	return read_port(pc_com, PORT_RX);
}

static void process_data(pc_com_t *pc_com) {
	while (has_data(pc_com)) {
		char c = read_data(pc_com);
		if (ringbuffer_write(&pc_com->rx_ringbuffer, &c, 1))
			semaphore_signal(&pc_com->rx_semaphore);
	}
}

static bool can_transmit(pc_com_t *pc_com) {
	return read_port(pc_com, PORT_LINE_STATUS) & LINE_STATUS_TX_BUFFER_EMPTY;
}

static void set_irqs(pc_com_t *pc_com, int irqs) {
	write_port(pc_com, PORT_INTERRUPT_ENABLE, irqs);
}

// expects I/O to be disabled
static void set_baud_divisor(pc_com_t *pc_com, int divisor) {
	int old_port = read_port(pc_com, PORT_LINE_CONTROL);
	write_port(pc_com, PORT_LINE_CONTROL, LINE_CONTROL_DIVISOR_LATCH);

	write_port(pc_com, PORT_DIVISOR_LOW, divisor & 0xff);
	write_port(pc_com, PORT_DIVISOR_HIGH, (divisor >> 8) & 0xff);

	write_port(pc_com, PORT_LINE_CONTROL, old_port);

	pc_com->current_divisor = divisor;
}

static void disable_io_and_flush_input(pc_com_t *pc_com) {
	write_port(pc_com, PORT_MODEM_CONTROL, 0);

	process_data(pc_com);
}

static void termios_callback(void *internal, termios_t *termios) {
	pc_com_t *pc_com = internal;

	int baud = termios_baud_to_number(termios);

	int new_divisor = (baud == 0) ? 1 : BASE_BAUD / baud;
	if (new_divisor == 0)
		new_divisor = 1;

	if (new_divisor != pc_com->current_divisor) {
		disable_io_and_flush_input(pc_com);

		set_baud_divisor(pc_com, new_divisor);
	}

	int line_control = (termios->c_cflag & CSTOPB) ? LINE_CONTROL_STOP_BITS : 0;

	switch (termios->c_cflag & CSIZE) {
		case CS6:
			line_control = LINE_CONTROL_WORD_LENGTH_6_BITS;
			break;
		case CS7:
			line_control = LINE_CONTROL_WORD_LENGTH_7_BITS;
			break;
		case CS8:
			line_control = LINE_CONTROL_WORD_LENGTH_8_BITS;	
			break;
		default:
	}

	if (termios->c_cflag & PARENB)
		line_control = (termios->c_cflag & PARODD) ? LINE_CONTROL_PARITY_ODD : LINE_CONTROL_PARITY_EVEN;

	write_port(pc_com, PORT_LINE_CONTROL, line_control);

	write_port(pc_com, PORT_MODEM_CONTROL, 
			MODEM_CONTROL_DATA_TERMINAL_READY | MODEM_CONTROL_REQUEST_TO_SEND | MODEM_CONTROL_AUX_1 | MODEM_CONTROL_AUX_2);
}

static void handler_thread(void) {
	pc_com_t *pc_com = current_thread()->kernelarg;

	for (;;) {
		semaphore_wait(&pc_com->rx_semaphore, false);

		char c;
		ringbuffer_read(&pc_com->rx_ringbuffer, &c, 1);
		tty_process(pc_com->tty, c);
	}
}

static size_t write_tty(void *internal, char *str, size_t size) {
	pc_com_t *pc_com = internal;

	for (int i = 0; i < size; ++i) {
		while (!can_transmit(pc_com));
		write_port(pc_com, PORT_TX, str[i]);
	}

	return size;
}

// TODO enqueue dpc and call tty_process directly from it once it is safe to be called from an interrupt context
static void irq(isr_t *self, context_t *) {
	pc_com_t *pc_com = self->priv;

	// check if there is data to be received
	process_data(pc_com);
}

static uacpi_iteration_decision set_com_resource(void *user, uacpi_resource *res) {
	pc_com_t *pc_com = user;
	switch (res->type) {
		case UACPI_RESOURCE_TYPE_IRQ:
			pc_com->irq = res->irq;
			break;
		case UACPI_RESOURCE_TYPE_IO:
			pc_com->io_base = res->io.minimum;
			break;
		default:
			printf("com%d: unknown resource type %d\n", pc_com->id, res->type);
	}

	return UACPI_ITERATION_DECISION_CONTINUE;
}

static int pc_com_id = 0;

static uacpi_iteration_decision init_com(void *, uacpi_namespace_node *node, unsigned int) {
	uacpi_resources *res;

	pc_com_t *pc_com = alloc(sizeof(pc_com_t));
	__assert(pc_com);
	__assert(ringbuffer_init(&pc_com->rx_ringbuffer, 1000) == 0);

	pc_com->id = pc_com_id++;
	SEMAPHORE_INIT(&pc_com->rx_semaphore, 0);

	// get io port and gsi
	uacpi_status ret = uacpi_get_current_resources(node, &res);
	if (ret) {
		printf("com%d: failed to get uacpi resources: %s\n", pc_com->id, uacpi_status_to_string(ret));
		return UACPI_ITERATION_DECISION_NEXT_PEER;
	}

	ret = uacpi_for_each_resource(res, set_com_resource, pc_com);
	if (ret) {
		printf("com%d: failed to process uacpi resources: %s\n", pc_com->id, uacpi_status_to_string(ret));
		return UACPI_ITERATION_DECISION_NEXT_PEER;
	}

	uacpi_free_resources(res);

	if (pc_com->irq.num_irqs != 1) {
		printf("com%d: unsupported irq count\n");
		return UACPI_ITERATION_DECISION_NEXT_PEER;
	}

	// set up port
	set_irqs(pc_com, INTERRUPT_ENABLE_NONE);
	set_baud_divisor(pc_com, 3);
	write_port(pc_com, PORT_LINE_CONTROL, LINE_CONTROL_WORD_LENGTH_8_BITS);
	set_irqs(pc_com, INTERRUPT_ENABLE_DATA_AVAILABLE);
	write_port(pc_com, PORT_MODEM_CONTROL, 
			MODEM_CONTROL_DATA_TERMINAL_READY | MODEM_CONTROL_REQUEST_TO_SEND | MODEM_CONTROL_AUX_1 | MODEM_CONTROL_AUX_2);
	
	// set up irq
	isr_t *isr = interrupt_allocate(irq, ARCH_EOI, IPL_SERIAL);
	__assert(isr);
	isr->priv = pc_com;

	#ifdef __x86_64__
		arch_ioapic_setirq(pc_com->irq.irqs[0], isr->id & 0xff, current_cpu_id(), false);
	#else
		__assert(!"TODO");
	#endif

	pc_com->handler_thread = sched_newthread(handler_thread, PAGE_SIZE, 0, NULL, NULL);
	__assert(pc_com->handler_thread);
	pc_com->handler_thread->kernelarg = pc_com;
	sched_queue(pc_com->handler_thread);

	char tty_name[10];
	snprintf(tty_name, 10, "com%d", pc_com->id);

	pc_com->tty = tty_create(tty_name, write_tty, NULL, termios_callback, NULL, pc_com);
	if (pc_com->tty == NULL) {
		printf("%s: failed to create tty\n", tty_name);
		return UACPI_ITERATION_DECISION_NEXT_PEER;
	}

	printf("com%d: port %04x isr %x\n", pc_com->id, pc_com->io_base, isr->id);

	return UACPI_ITERATION_DECISION_CONTINUE;
}

static void pc_com_find_devices(void) {
	uacpi_find_devices("PNP0500", init_com, NULL);
	uacpi_find_devices("PNP0501", init_com, NULL);
}

// TODO: when device system is rewritten, have this be discovered by the acpi bus instead
INIT_ROUTINE_DEFINE(pc_com, INIT_ROUTINE_FLAGS_NONE, pc_com_find_devices, acpi, tty);
