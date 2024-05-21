#include <kernel/net.h>
#include <kernel/sock.h>
#include <ringbuffer.h>

#define WORKER_COUNT 8
#define WORKER_BUFFER_SIZE (64 * 1024)
#define WORKER_INTERNAL_BUFFER_SIZE 2048
// MSS will be min(MSS_LIMIT, mtu - sizeof(ipv4header_t) - sizeof(tcpheader_t))
#define MSS_LIMIT = (WORKER_INTERNAL_BUFFER_SIZE - sizeof(tcpheader_t))

typedef struct {
	spinlock_t lock;
	semaphore_t semaphore;
	// format:
	// ipv4 pseudo header
	// tcp packet
	ringbuffer_t ringbuffer;
} tcpworker_t;

static tcpworker_t worker[WORKER_COUNT];
static uint64_t currentworker;

typedef struct {
	uint32_t peer;
	uint16_t peerport;
	uint16_t localport;
} connkey_t;

typedef struct {
	uint16_t srcport;
	uint16_t dstport;
	uint32_t seq;
	uint32_t ack;
	uint8_t dataoffset;
	uint8_t control;
	uint16_t window;
	uint16_t checksum;
	uint16_t urgentptr;
} __attribute__((packed)) tcpheader_t;

typedef struct {
	uint32_t source;
	uint32_t dest;
	uint8_t zero;
	uint8_t protocol;
	uint16_t length;
} __attribute__((packed)) ipv4pseudoheader_t;

#define TCP_STATE_LISTEN 0 // listening for connections
#define TCP_STATE_SYNSENT 1 // sent a connection request
#define TCP_STATE_SYNRECEIVED 2 // received a connection request, waiting for ACK
#define TCP_STATE_ESTABILISHED 3 // got connection ACK, connection is oppen
#define TCP_STATE_FINWAIT1 4 // waiting for connection termination from peer or an ack for the already sent FIN
#define TCP_STATE_FINWAIT2 5 // waiting for connection termination from peer
#define TCP_STATE_CLOSEWAIT 7 // waiting for a termination request from local user
#define TCP_STATE_CLOSING 8 // waiting for termination ack

typedef struct {
	spinlock_t lock;
	int state;
	ringbuffer_t ringbuffer;

	uint32_t snduna;
	uint32_t sndnext;
	uint32_t sndwindow;
	uint32_t sndurgent;
	uint32_t sndseqwl;
	uint32_t sndackwl;
	uint32_t iss;

	uint32_t rcvnext;
	uint32_t rcvwindow;
	uint32_t rcvurgent;
	uint32_t irs;
} tcb_t;

// TODO dpc:
// construct ipv4header
// calculate checksum
// check against WORKER_INTERNAL_BUFFER_SIZE

// ran in DPC context
void tcp_process(netdev_t *netdev, void *buffer, ipv4frame_t *ipv4frame) {
	
}

__attribute__((noreturn)) static void tcp_worker() {
	tcpworker_t *self = _cpu()->thread->kernelarg;
	ipv4pseudoheader_t ipv4;
	uint8_t buffer[WORKER_INTERNAL_BUFFER_SIZE];

	for (;;) {
		// wait for a task
		semaphore_wait(&self->semaphore, false);

		interrupt_set(false);
		spinlock_acquire(&self->lock);

		// read pseudo ipv4 header constructed by tcp_process
		// the checksum has already been calculated by it
		__assert(ringbuffer_read(&self->ringbuffer, &ipv4, sizeof(ipv4pseudoheader_t)) == sizeof(ipv4pseudoheader_t));

		// and then the actual tcp packet
		__assert(ringbuffer_read(&self->ringbuffer, buffer, ipv4.length) == ipv4.length);

		spinlock_release(&self->lock);
		interrupt_set(true);

		tcpheader_t *tcpheader = buffer;

		// TODO get tcb of connection (if any)
	}
}

void tcp_init() {
	for (int i = 0; i < WORKER_COUNT; ++i) {
		// initialize worker threads

		SPINLOCK_INIT(worker[i].lock);
		SEMAPHORE_INIT(&worker[i].semaphore, 0);
		__assert(ringbuffer_init(&worker[i].ringbuffer, WORKER_BUFFER_SIZE) == 0);
		thread_t *thread = sched_newthread(tcp_worker, PAGE_SIZE * 3, 0, NULL, NULL);
		__assert(thread);
		thread->kernelarg = &worker[i];
		sched_queue(thread);
	}
}
