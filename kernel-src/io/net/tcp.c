#include <kernel/net.h>
#include <kernel/sock.h>
#include <ringbuffer.h>
#include <kernel/itimer.h>
#include <kernel/timekeeper.h>
#include <kernel/interrupt.h>

#define WORKER_COUNT 8
#define WORKER_BUFFER_SIZE (64 * 1024)
#define WORKER_INTERNAL_BUFFER_SIZE 2048
// MSS will be min(_MSS_LIMIT, mtu - sizeof(ipv4header_t) - sizeof(tcpheader_t))
#define _MSS_LIMIT (WORKER_INTERNAL_BUFFER_SIZE - sizeof(tcpheader_t))
#define MSS_LIMIT(mtu) min(_MSS_LIMIT, (mtu) - sizeof(ipv4frame_t) - sizeof(tcpheader_t))

#define TASK_TYPE_PACKET 0
#define TASK_TYPE_TIMEOUT 1
#define TASK_TYPE_CLOSE 2

typedef struct {
	spinlock_t lock;
	semaphore_t semaphore;
	// format:
	// int tasktype
	// case TASK_TYPE_PACKET:
	// 	ipv4 pseudo header
	// 	tcp packet
	// 	--
	// case TASK_TYPE_TIMEOUT:
	// case TASK_TYPE_CLOSE:
	// 	tcb_t *tcb
	// 	--
	ringbuffer_t ringbuffer;
} tcpworker_t;

typedef struct {
	uint32_t peer;
	uint16_t peerport;
	uint16_t localport;
} connkey_t;

#define CONTROL_URG (1 << 5)
#define CONTROL_ACK (1 << 4)
#define CONTROL_PSH (1 << 3)
#define CONTROL_RST (1 << 2)
#define CONTROL_SYN (1 << 1)
#define CONTROL_FIN (1 << 0)

#define OPTIONS_MSS_KIND 2
#define OPTIONS_MSS_LEN 4
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

#define TCB_STATE_ABORT -1 // ignore any further packets
#define TCB_STATE_CLOSED 0
#define TCB_STATE_LISTEN  1// listening for connections
#define TCB_STATE_SYNSENT 2 // sent a connection request
#define TCB_STATE_SYNRECEIVED 3 // received a connection request, waiting for ACK
#define TCB_STATE_ESTABILISHED 4 // got connection ACK, connection is open
#define TCB_STATE_FINWAIT1 5 // waiting for connection termination from peer or an ack for the already sent FIN
#define TCB_STATE_FINWAIT2 6 // waiting for connection termination from peer
#define TCB_STATE_CLOSEWAIT 7 // waiting for a termination request from local user
#define TCB_STATE_CLOSING 8 // waiting for termination ack
#define TCB_STATE_LASTACK 9 // waiting for termination ack
#define TCB_STATE_TIMEWAIT 10 // waiting to release resources

#define RTO_START_SEC 1
#define RTO_MAX_SEC 64
#define TCB_RINGBUFFER_SIZE (1024 * 64 - 1)
#define MSL_SEC 5

#define TCB_HOLD(tcb) __atomic_add_fetch(&(tcb)->refcount, 1, __ATOMIC_SEQ_CST);
#define TCB_RELEASE(tcb) { \
	if (__atomic_sub_fetch(&(tcb)->refcount, 1, __ATOMIC_SEQ_CST) == 0) { \
		inactivetcb(tcb); \
		(tcb) = NULL; \
	} \
}

#define TRANSMISSION_QUEUE_SIZE 5
#define RECEIVE_QUEUE_SIZE 5

#define QUEUE_HEADER_SIZE (sizeof(queuedpacket_t *) + sizeof(size_t))

// queued incoming data packets
typedef struct queuedpacket_t {
	struct queuedpacket_t *next;
	size_t datalen;
	tcpheader_t tcpheader;
	// data is here, based on packet len
} queuedpacket_t;

typedef struct tcb_t {
	mutex_t mutex;
	int state;
	ringbuffer_t receivebuffer;
	int refcount;
	int port;
	ringbuffer_t transmitbuffer;
	size_t retransmitpacketlen;
	void *retransmitbuffer; // mtu sized buffer
	struct tcb_t *parent; // for connections from a listening socket
	uint32_t self;
	connkey_t key;
	pollheader_t pollheader;
	bool shouldclose;
	size_t backlogfree;
	ringbuffer_t backlog;
	bool reset;

	itimer_t itimer;
	int currentrto;
	timespec_t lastsend;

	queuedpacket_t *receivequeue;

	uint32_t sndmss;
	uint32_t sndunack; // oldest unacknowledged sequence number
	uint32_t sndnext; // next sequence number to be send
	uint32_t sndwindow; // number of bytes willing to be accepted
	uint32_t sndurgent; // urgent pointer
	uint32_t sndseqwl; // seq for last window update
	uint32_t sndackwl; // ack for last window update
	uint32_t iss; // initial sequence number

	uint32_t rcvmss;
	uint32_t rcvnext; // next byte expected
	uint32_t rcvwindow; // number of bytes willing to be received
	uint32_t rcvurgent; // receive urgent pointer
	uint32_t irs; // initial receive number
} tcb_t;

typedef struct {
	socket_t socket;
	tcb_t *tcb;
} tcpsocket_t;

// for local port allocation
#define PORT_COUNT 65536
#define START_PORT 50000 // multiple of 64
static uint64_t portbitmap[PORT_COUNT / 64];
static spinlock_t portlock;
static int currentport = START_PORT;

#define PORT_GET(port) (portbitmap[(port)/ 64] & (1lu << ((port) % 64)))
#define PORT_ALLOCATE(port) portbitmap[(port) / 64] |= (1lu << ((port) % 64))
#define PORT_FREE(port) portbitmap[(port) / 64] &= ~(1lu << ((port) % 64))

static int allocateport(int *port) {
	int error = 0;
	bool intstatus = interrupt_set(false);
	spinlock_acquire(&portlock);

	if (*port) {
		// allocate port asked for
		if (PORT_GET(*port)) {
			error = EADDRINUSE;
			goto cleanup;
		}

		PORT_ALLOCATE(*port);
	} else {
		// find some port to use, looping through all of them to reduce port reuse.
		int allocatedport = 0;
		int startingport = currentport;
		do {
			if (PORT_GET(currentport) == 0)
				allocatedport = currentport;

			int newcurrentport = (currentport + 1) % PORT_COUNT;
			// overflow check
			if (newcurrentport < currentport)
				currentport = START_PORT;
			else
				currentport = newcurrentport;
		} while (allocatedport == 0 && startingport != currentport);

		if (allocatedport == 0) {
			error = EADDRINUSE;
			goto cleanup;
		}

		*port = allocatedport;
		PORT_ALLOCATE(*port);
	}

	cleanup:
	spinlock_release(&portlock);
	interrupt_set(intstatus);
	return error;
}

static void freeport(int port) {
	__assert(port);
	bool intstatus = interrupt_set(false);
	spinlock_acquire(&portlock);

	PORT_FREE(port);

	spinlock_release(&portlock);
	interrupt_set(intstatus);
}

static tcpworker_t workers[WORKER_COUNT];
static uint64_t currentworker;

#define WORKER_RETRIES WORKER_COUNT
// returns with worker locked
static tcpworker_t *getworker(size_t packetlen, int tasktype) {
	size_t buffersize = sizeof(int);
	if (tasktype == TASK_TYPE_PACKET) {
		buffersize += packetlen + sizeof(ipv4pseudoheader_t);
	} else if (tasktype == TASK_TYPE_TIMEOUT || tasktype == TASK_TYPE_CLOSE) {
		buffersize += sizeof(tcb_t *);
	} else {
		__assert(!"Bad task type");
	}

	tcpworker_t *goodworker = NULL;
	for (int i = 0; i < WORKER_RETRIES; ++i) {
		uint64_t workern;
		workern = __atomic_fetch_add(&currentworker, 1, __ATOMIC_SEQ_CST) % WORKER_COUNT;
		tcpworker_t *worker = &workers[workern];
		spinlock_acquire(&worker->lock);
		if (buffersize <= WORKER_BUFFER_SIZE - RINGBUFFER_DATACOUNT(&worker->ringbuffer)) {
			// found a suitable worker!
			goodworker = worker;
			break;
		}
		spinlock_release(&worker->lock);
	}

	return goodworker;
}

// for getting the tcb of a connection
// listening tcbs are treated in a special way:
// they have a peer port number of 0.
// the children sockets will have a pointer to it
// and they will hold a reference as to keep its port allocated.
static hashtable_t conntable;
static mutex_t conntablemutex;

static void timeoutdpc(context_t *, dpcarg_t arg) {
	tcb_t *tcb = arg;
	int tasktype = TASK_TYPE_TIMEOUT;
	tcpworker_t *worker = getworker(0, tasktype);
	TCB_HOLD(tcb);
	__assert(ringbuffer_write(&worker->ringbuffer, &tasktype, sizeof(tasktype)) == sizeof(tasktype));
	__assert(ringbuffer_write(&worker->ringbuffer, &tcb, sizeof(tcb_t *)) == sizeof(tcb_t *));
	semaphore_signal(&worker->semaphore);
	spinlock_release(&worker->lock);
}

static tcb_t *allocatetcb(size_t mtu) {
	tcb_t *tcb = alloc(sizeof(tcb_t));
	if (tcb == NULL)
		return NULL;

	tcb->retransmitbuffer = alloc(mtu);
	if (tcb->retransmitbuffer == NULL) {
		free(tcb);
		return NULL;
	}

	if (ringbuffer_init(&tcb->receivebuffer, TCB_RINGBUFFER_SIZE)) {
		free(tcb->retransmitbuffer);
		free(tcb);
		return NULL;
	}
	
	if (ringbuffer_init(&tcb->transmitbuffer, TCB_RINGBUFFER_SIZE)) {
		ringbuffer_destroy(&tcb->receivebuffer);
		free(tcb->retransmitbuffer);
		free(tcb);
		return NULL;
	}

	POLL_INITHEADER(&tcb->pollheader);
	MUTEX_INIT(&tcb->mutex);
	tcb->refcount = 1;

	itimer_init(&tcb->itimer, timeoutdpc, tcb);
	return tcb;
}

// itimer should be PAUSED when this gets called.
static void inactivetcb(tcb_t *tcb) {
	__assert(tcb->itimer.paused);

	queuedpacket_t *iterator = tcb->receivequeue;
	while (iterator) {
		queuedpacket_t *p = iterator;
		iterator = iterator->next;
		free(p);
	}

	if (tcb->port)
		freeport(tcb->port);

	if (tcb->state == TCB_STATE_LISTEN)
		ringbuffer_destroy(&tcb->backlog);

	tcb_t *parent = tcb->parent;
	free(tcb->retransmitbuffer);
	ringbuffer_destroy(&tcb->transmitbuffer);
	ringbuffer_destroy(&tcb->receivebuffer);
	free(tcb);

	if (parent)
		TCB_RELEASE(parent);
}

static tcb_t *tcbget(connkey_t *key, bool getlistening) {
	MUTEX_ACQUIRE(&conntablemutex, false);
	void *v;
	tcb_t *tcb;

	if (hashtable_get(&conntable, &v, key, sizeof(connkey_t)))
		tcb = NULL;
	else
		tcb = v;

	connkey_t listenkey = *key;
	listenkey.peerport = 0;
	listenkey.peer = 0;
	if (tcb == NULL && getlistening && hashtable_get(&conntable, &v, &listenkey, sizeof(connkey_t)) == 0)
		tcb = v;

	if (tcb)
		TCB_HOLD(tcb);

	MUTEX_RELEASE(&conntablemutex);
	return tcb;
}

static int tcbset(tcb_t *tcb, connkey_t *key, bool remove) {
	MUTEX_ACQUIRE(&conntablemutex, false);

	int error;
	if (remove) {
		error = hashtable_remove(&conntable, key, sizeof(connkey_t));
		TCB_RELEASE(tcb);
	} else {
		TCB_HOLD(tcb);
		error = hashtable_set(&conntable, tcb, key, sizeof(connkey_t), true);
	}

	MUTEX_RELEASE(&conntablemutex);
	return error;
}

// don't touch this, it works and the compiler isn't too happy about it
// expects the header to be in network byte order already
static uint16_t checksum(void *headerbuff, void *buffer) {
	tcpheader_t *tcpheader = buffer;
	ipv4pseudoheader_t *header = headerbuff;
	uint16_t len = be_to_cpu_w(header->length);
	uint32_t sum = 0;
	uint16_t *pseudo = headerbuff;
	uint16_t *packet = buffer;
	size_t headerlen = (tcpheader->dataoffset >> 4) * 4;
	size_t datalen = len - headerlen;
	uint16_t *data = (uint16_t *)((uintptr_t)buffer + headerlen);

	// pseudoheader
	for (int i = 0; i < sizeof(ipv4pseudoheader_t) / 2; ++i)
		sum += be_to_cpu_w(pseudo[i]);

	// tcp header
	for (int i = 0; i < headerlen / 2; ++i)
		sum += be_to_cpu_w(packet[i]);

	// data
	while (datalen > 1) {
		sum += be_to_cpu_w(*data++);
		datalen -= 2;
	}

	if (datalen)
		sum += be_to_cpu_w(*(uint8_t *)data);

	sum = (sum >> 16) + (sum & 0xffff);
	sum = sum + (sum >> 16);
	return (uint16_t)~sum;
}

// ran in DPC context
void tcp_process(netdev_t *netdev, void *buffer, ipv4frame_t *ipv4frame) {
	// this has to be in network byte order
	ipv4pseudoheader_t ipv4pseudoheader = {
		.source = ipv4frame->srcaddr,
		.dest = ipv4frame->dstaddr,
		.zero = 0,
		.protocol = ipv4frame->protocol,
		// ipv4 len is header + size
		.length = cpu_to_be_w(be_to_cpu_w(ipv4frame->packetlen) - sizeof(ipv4frame_t))
	};

	if (checksum(&ipv4pseudoheader, buffer) != 0) {
		printf("tcp: bad checksum\n");
		return;
	}

	if (be_to_cpu_w(ipv4pseudoheader.length) > WORKER_INTERNAL_BUFFER_SIZE) {
		printf("tcp: packet too big for internal buffer size\n");
		return;
	}

	int tasktype = TASK_TYPE_PACKET;
	tcpworker_t *worker = getworker(ipv4pseudoheader.length, tasktype);
	if (worker == NULL) {
		printf("tcp: no free workers to handle packet\n");
		return;
	}

	ipv4pseudoheader.source = cpu_to_be_d(ipv4pseudoheader.source);
	ipv4pseudoheader.dest = cpu_to_be_d(ipv4pseudoheader.dest);
	ipv4pseudoheader.length = cpu_to_be_w(ipv4pseudoheader.length);

	tcpheader_t *header = buffer;
	header->srcport = be_to_cpu_w(header->srcport);
	header->dstport = be_to_cpu_w(header->dstport);
	header->seq = be_to_cpu_d(header->seq);
	header->ack = be_to_cpu_d(header->ack);
	header->window = be_to_cpu_w(header->window);
	header->urgentptr = be_to_cpu_w(header->urgentptr);

	__assert(ringbuffer_write(&worker->ringbuffer, &tasktype, sizeof(tasktype)) == sizeof(tasktype));
	__assert(ringbuffer_write(&worker->ringbuffer, &ipv4pseudoheader, sizeof(ipv4pseudoheader_t)) == sizeof(ipv4pseudoheader_t));
	__assert(ringbuffer_write(&worker->ringbuffer, buffer, ipv4pseudoheader.length) == ipv4pseudoheader.length);
	semaphore_signal(&worker->semaphore);

	spinlock_release(&worker->lock);
}

static int tcp_sendpacket(tcpheader_t *header, size_t buffersize, uint32_t peer, uint32_t self) {
	ipv4pseudoheader_t pseudoheader = {
		.source = cpu_to_be_d(self),
		.dest = cpu_to_be_d(peer),
		.zero = 0,
		.protocol = IPV4_PROTO_TCP,
		.length = cpu_to_be_w(buffersize)
	};

	// put the header in network byte order
	header->srcport = cpu_to_be_w(header->srcport);
	header->dstport = cpu_to_be_w(header->dstport);
	header->seq = cpu_to_be_d(header->seq);
	header->ack = cpu_to_be_d(header->ack);
	header->window = cpu_to_be_w(header->window);
	header->urgentptr = cpu_to_be_w(header->urgentptr);
	header->checksum = 0;
	header->checksum = cpu_to_be_w(checksum(&pseudoheader, header));

	// dispatch it to the next layer
	return ipv4_sendpacket(header, buffersize, peer, IPV4_PROTO_TCP, NULL);
}

// assumes the data is already in place (if any, could be only the header too)
static void tcp_createheader(tcpheader_t *header, tcb_t *tcb, size_t packetlen,
		void *options, size_t optionslen, uint8_t control, uint16_t urgentp) {
	__assert((optionslen % sizeof(uint32_t)) == 0);

	header->srcport = tcb->key.localport;
	header->dstport = tcb->key.peerport;
	header->seq = tcb->sndnext;
	header->ack = tcb->rcvnext;
	header->dataoffset = ((sizeof(tcpheader_t) + optionslen) / 4) << 4;
	header->control = control;
	header->window = tcb->rcvwindow;
	header->urgentptr = urgentp;
	if (options)
		memcpy((void *)(header + 1), options, optionslen);
}

static inline bool seqinrange(uint32_t seq, uint32_t seqend, uint32_t rangestart, uint32_t rangeend) {
	if (rangeend < rangestart) {
		// range overflowed!
		if ((seqend <= rangestart && seqend > rangeend) || (seq < rangestart && seq >= rangeend))
			return false;
	} else {
		// range not overflowed
		if (!(seq >= rangestart && seqend <= rangeend))
			return false;
	}

	return true;
}

// returns true if an acknowledgement should be sent
static bool tcp_queuereceivepacket(tcb_t *tcb, tcpheader_t *tcpheader, size_t packetlen) {
	size_t headerlen = (tcpheader->dataoffset >> 4) * 4;
	size_t datalen = packetlen - headerlen;	
	// first check if segment is in window

	// too big for window
	if (datalen > tcb->rcvwindow) {
		return true;
	}

	if (datalen == 0) {
		return false;
	}

	uint32_t seqend = tcpheader->seq + datalen;
	uint32_t windowend = tcb->rcvnext + tcb->rcvwindow;

	// is inside the window?
	if (seqinrange(tcpheader->seq, seqend, tcb->rcvnext, windowend) == false) {
		return true;
	}

	// check if it conflicts with any received segment (and drop those if so)
	queuedpacket_t *iterator = tcb->receivequeue;
	queuedpacket_t *previous = NULL;
	while (iterator) {
		uint32_t iteratorseqend = iterator->tcpheader.seq + iterator->datalen;
		if (	seqinrange(tcpheader->seq, seqend, iterator->tcpheader.seq, iteratorseqend) || // new segment is inside old segment
			seqinrange(iterator->tcpheader.seq, iterator->tcpheader.seq + 1, tcpheader->seq, seqend) || // start of old segment is inside new segment
			seqinrange(iteratorseqend - 1, iteratorseqend, tcpheader->seq, seqend)) { // end of old segment is inside new segment
			// we can drop this segment
			if (previous)
				previous->next = iterator->next;
			else
				tcb->receivequeue = iterator->next;

			queuedpacket_t *forfree = iterator;
			iterator = iterator->next;
			free(forfree);
		} else {
			// don't drop this one
			previous = iterator;
			iterator = iterator->next;
		}
	}

	// segment is within window!
	// is it the next expected segment?
	if (tcpheader->seq == tcb->rcvnext) {
		// it is, flush into the user readable buffer
		size_t written = 0;
		__assert(ringbuffer_write(&tcb->receivebuffer, (void *)((uintptr_t)tcpheader + headerlen), datalen) == datalen);
		tcb->rcvnext += datalen;
		written += datalen;

		// and check if there are any queued segments that we can flush as well
		queuedpacket_t *iterator = tcb->receivequeue;
		queuedpacket_t *previous = NULL;
		while (iterator) {
			if (iterator->tcpheader.seq == tcb->rcvnext) {
				size_t iteratorheaderlen = (iterator->tcpheader.dataoffset >> 4) * 4;
				// we can flush this segment
				__assert(ringbuffer_write(&tcb->receivebuffer, (void *)((uintptr_t)&iterator->tcpheader + iteratorheaderlen), iterator->datalen) == iterator->datalen);
				tcb->rcvnext += iterator->datalen;
				written += iterator->datalen;

				if (previous)
					previous->next = iterator->next;
				else
					tcb->receivequeue = iterator->next;

				queuedpacket_t *forfree = iterator;
				iterator = iterator->next;
				free(forfree);
			} else {
				// no more segments to flush (they are ordered in the linked list by sequence number
				break;
			}
		}

		__assert(written <= tcb->rcvwindow);
		tcb->rcvwindow -= written;
		poll_event(&tcb->pollheader, POLLIN);
		return true;
	}

	// it isn't, queue it
	queuedpacket_t *queuedpacket = alloc(QUEUE_HEADER_SIZE + packetlen);
	if (queuedpacket == NULL)
		return false; // if we can't allocate memory to queue it, just wait for a retransmission

	queuedpacket->datalen = datalen;
	memcpy(&queuedpacket->tcpheader, tcpheader, packetlen);

	// find where to insert it
	// (there are no collisions here, which can be used to calculate the range 
	// between the end of the previous segment and the start of the next one)
	iterator = tcb->receivequeue;
	previous = NULL;
	while (iterator) {
		if (seqinrange(tcpheader->seq, seqend, previous ? previous->tcpheader.seq + previous->datalen : tcb->rcvnext, iterator->tcpheader.seq))
			break;

		previous = iterator;
		iterator = iterator->next;
	}

	if (previous) {
		// insert packet
		previous->next = queuedpacket;
		queuedpacket->next = iterator;
	} else {
		// no segments before this one, put it in front of the queue
		queuedpacket->next = tcb->receivequeue;
		tcb->receivequeue = queuedpacket;
	}
	
	return false;
}

// sends back an acknowledgement
static int tcp_ack(tcb_t *tcb) {
	uint32_t self = ipv4_getnetdevip(tcb->key.peer);
	size_t packetlen = sizeof(tcpheader_t);
	tcpheader_t header;
	tcp_createheader(&header, tcb, packetlen, NULL, 0, CONTROL_ACK, 0);

	return tcp_sendpacket(&header, packetlen, tcb->key.peer, self);
}

static int tcp_sendreset(tcpheader_t *tcpheader, connkey_t *key, ipv4pseudoheader_t *ipv4) {
	tcpheader_t header;
	size_t dataoffset = (tcpheader->dataoffset >> 4) * 4;
	size_t datasize = ipv4->length - dataoffset;
	tcb_t tcb = {
		.key = *key,
		.sndnext = tcpheader->ack,
		.rcvnext = tcpheader->seq + ((tcpheader->control & (CONTROL_FIN | CONTROL_SYN)) ? 1 : 0) + datasize
	};

	tcp_createheader(&header, &tcb, sizeof(header), NULL, 0, CONTROL_RST | CONTROL_ACK, 0);
	uint32_t self = ipv4_getnetdevip(key->peer);
	return tcp_sendpacket(&header, sizeof(header), key->peer, self);
}

// transmits the next segment in the transmit ringbuffer
static int tcp_transmitnextsegment(tcb_t *tcb) {
	itimer_pause(&tcb->itimer, NULL, NULL);
	uint32_t self = ipv4_getnetdevip(tcb->key.peer);
	tcpheader_t *header = tcb->retransmitbuffer;
	tcb->retransmitpacketlen = ringbuffer_read(&tcb->transmitbuffer, header + 1, tcb->sndmss) + sizeof(tcpheader_t);
	__assert(tcb->retransmitpacketlen > sizeof(tcpheader_t));
	tcp_createheader(header, tcb, tcb->retransmitpacketlen, NULL, 0, CONTROL_ACK | CONTROL_PSH, 0);

	int error = tcp_sendpacket(header, tcb->retransmitpacketlen, tcb->key.peer, self);
	tcb->sndnext += tcb->retransmitpacketlen - sizeof(tcpheader_t);
	tcb->currentrto = RTO_START_SEC;
	tcb->lastsend = timekeeper_timefromboot();
	itimer_set(&tcb->itimer, RTO_START_SEC * 1000000, 0);
	itimer_resume(&tcb->itimer);

	return error;
}

// on a connection reset or error
static void tcp_reset(tcb_t *tcb) {
	itimer_pause(&tcb->itimer, NULL, NULL);
	tcbset(tcb, &tcb->key, true);
	tcb->reset = true;
	tcb->state = TCB_STATE_ABORT;
	poll_event(&tcb->pollheader, POLLERR);
}

// first time a fin is sent (not on retransmissions, this sets up the closed socket state
// as we could still have been transmitting data on the close call)
static void tcp_dofirstfin(tcb_t *tcb) {
	itimer_pause(&tcb->itimer, NULL, NULL);
	tcb->state = tcb->state == TCB_STATE_CLOSEWAIT ? TCB_STATE_LASTACK : TCB_STATE_FINWAIT1;

	uint32_t self = ipv4_getnetdevip(tcb->key.peer);
	tcb->retransmitpacketlen = sizeof(tcpheader_t);
	tcp_createheader(tcb->retransmitbuffer, tcb, tcb->retransmitpacketlen, NULL, 0, CONTROL_FIN | CONTROL_ACK, 0);

	tcp_sendpacket(tcb->retransmitbuffer, tcb->retransmitpacketlen, tcb->key.peer, self);

	itimer_set(&tcb->itimer, RTO_START_SEC * 1000000, 0);
	tcb->currentrto = RTO_START_SEC;
	tcb->lastsend = timekeeper_timefromboot();
	itimer_resume(&tcb->itimer);
}

// handles an ack from the peer
static void tcp_handleack(tcb_t *tcb, tcpheader_t *tcpheader) {
	// prepare to transmit some more data back
	tcb->sndunack = tcpheader->ack;
	if (RINGBUFFER_DATACOUNT(&tcb->transmitbuffer)) {
		tcp_transmitnextsegment(tcb);
	} else if (tcb->shouldclose) {
		tcp_dofirstfin(tcb);
	} else {
		// nothing to send for now
		itimer_pause(&tcb->itimer, NULL, NULL);
	}
}

static void tcp_handledatareceive(tcb_t *tcb, tcpheader_t *tcpheader, ipv4pseudoheader_t *ipv4, int finstate) {
	bool fin = tcpheader->control & CONTROL_FIN;
	bool shouldack = tcp_queuereceivepacket(tcb, tcpheader, ipv4->length);

	// peer has closed the connection.
	// the next state to switch to will be up to the caller
	// TODO probably not return POLLHUP here, as data can still be sent to the peer.
	if (fin) {
		tcb->state = finstate;
		tcb->rcvnext += 1;
		shouldack = true;
		poll_event(&tcb->pollheader, POLLHUP);
	}

	if (shouldack)
		tcp_ack(tcb);

	if ((tcpheader->control & CONTROL_ACK) && tcb->sndnext == tcpheader->ack) {
		tcb->sndwindow = tcpheader->window;
		// some packet has been acked!
		tcp_handleack(tcb, tcpheader);
	}
}

static void tcp_handleclose(tcb_t *tcb) {
	switch (tcb->state) {
		case TCB_STATE_LISTEN:
			// for a listening socket we also have to flush out the backlog.
			tcb_t *backlogtcb;
			while (ringbuffer_read(&tcb->backlog, &backlogtcb, sizeof(tcb_t *))) {
				MUTEX_ACQUIRE(&backlogtcb->mutex, false);
				tcp_handleclose(backlogtcb);
				MUTEX_RELEASE(&backlogtcb->mutex);
			}
		case TCB_STATE_SYNSENT:
			// on a close, since we still don't have an estabilished connection yet
			// (we only sent the SYN or are only listening), 
			// we will simply remove the tcb from the connection
			// table and any SYN+ACK received will be met with an RST.
			tcbset(tcb, &tcb->key, true);
			itimer_pause(&tcb->itimer, NULL, NULL);
			tcb->state = TCB_STATE_CLOSED; // and any further packets will be ignored
			break;
		case TCB_STATE_ESTABILISHED:
		case TCB_STATE_SYNRECEIVED:
		case TCB_STATE_CLOSEWAIT:
			// if theres no more data to transmit, send a FIN and switch to TCB_STATE_FINWAIT1.
			// otherwise, set the shouldclose flag and continue transmitting what remains.
			// as no new data can come in now
			if (RINGBUFFER_DATACOUNT(&tcb->transmitbuffer) == 0)
				tcp_dofirstfin(tcb);
			else
				tcb->shouldclose = true;
			break;
		default:
			__assert(!"Closing an already closed connection");
	}
}

static void tcp_retransmit(tcb_t *tcb) {
	ipv4_sendpacket(tcb->retransmitbuffer, tcb->retransmitpacketlen, tcb->key.peer, IPV4_PROTO_TCP, NULL);
}

static void tcp_handletimeout(tcb_t *tcb) {
	itimer_pause(&tcb->itimer, NULL, NULL);

	if (tcb->currentrto == RTO_MAX_SEC || tcb->state == TCB_STATE_TIMEWAIT) {
		// timed out too much, reset connection
		tcp_reset(tcb);
		return;
	}

	uintmax_t nextwaitus;
	timespec_t now = timekeeper_timefromboot();
	if (timespec_diffms(now, tcb->lastsend) / 1000 >= tcb->currentrto) {
		// retransmit buffer
		tcp_retransmit(tcb);
		tcb->lastsend = timekeeper_timefromboot();
		tcb->currentrto *= 2;
		nextwaitus = tcb->currentrto * 1000000;
	} else {
		// set timer back up
		nextwaitus = timespec_diffms(now, tcb->lastsend) * 1000;
	}
	
	itimer_set(&tcb->itimer, nextwaitus, 0);
	itimer_resume(&tcb->itimer);
}

#define TCP_DEFAULT_MSS 536
static uint32_t getsendmss(tcpheader_t *tcpheader, tcb_t *tcb) {
	size_t optionslength = (tcpheader->dataoffset - 4) * 4;
	size_t newsendmss = TCP_DEFAULT_MSS;

	if (optionslength) {
		uint8_t* options = (uint8_t *)(tcpheader + 1);
		while (optionslength) {
			if (*options == 0) { // end
				break;
			} else if (*options == 1) { // nop
				++options;
				continue;
			} else if (*options == OPTIONS_MSS_KIND) {
				options += 2;
				newsendmss = be_to_cpu_w(*(uint16_t *)options);
				break;
			} else {
				options += options[1];
				continue;
			}
		}
	}

	return min(tcb->sndmss, newsendmss);
}

static long current = 0xdeadbeefbadc0ffe;

static uint32_t getrand32() {
	current += timekeeper_timefromboot().ns * timekeeper_timefromboot().s - timekeeper_time().ns * timekeeper_time().s + timekeeper_time().s;
	return current & 0xffffffff;
}

typedef struct {
	uint8_t msskind;
	uint8_t msslen;
	uint16_t mss;
} __attribute__((packed)) synoptions_t;

__attribute__((noreturn)) static void tcp_worker() {
	tcpworker_t *self = _cpu()->thread->kernelarg;
	ipv4pseudoheader_t ipv4;
	int tasktype;
	tcb_t *tcb = NULL;
	uint8_t buffer[WORKER_INTERNAL_BUFFER_SIZE];

	for (;;) {
		// wait for a task
		semaphore_wait(&self->semaphore, false);

		interrupt_set(false);
		spinlock_acquire(&self->lock);

		// read task type
		__assert(ringbuffer_read(&self->ringbuffer, &tasktype, sizeof(tasktype)) == sizeof(tasktype));
		tcpheader_t *tcpheader = (tcpheader_t *)buffer;

		if (tasktype == TASK_TYPE_TIMEOUT) {
			// read the tcb pointer
			__assert(ringbuffer_read(&self->ringbuffer, &tcb, sizeof(tcb)) == sizeof(tcb));
			spinlock_release(&self->lock);
			interrupt_set(true);
		} else if (tasktype == TASK_TYPE_PACKET) {
			// read pseudo ipv4 header constructed by tcp_process
			// the checksum has already been calculated by the dpc
			__assert(ringbuffer_read(&self->ringbuffer, &ipv4, sizeof(ipv4pseudoheader_t)) == sizeof(ipv4pseudoheader_t));

			// and then the actual tcp packet
			__assert(ringbuffer_read(&self->ringbuffer, buffer, ipv4.length) == ipv4.length);
			spinlock_release(&self->lock);
			interrupt_set(true);

			// bad peer address will be ignored
			// bad ports will get an RST sent to them
			if (ipv4.source == 0)
				continue;

			connkey_t key = {
				.peer = ipv4.source,
				.peerport = tcpheader->srcport,
				.localport = tcpheader->dstport
			};

			tcb = tcbget(&key, (tcpheader->control & CONTROL_SYN) && (tcpheader->control & CONTROL_ACK) == 0);
			if (tcb == NULL) {
				// no local tcb to handle it
				// send back an RST to whoever sent us this package
				
				// if the packet is an RST we don't actually send an RST back
				if (tcpheader->control & CONTROL_RST)
					continue;

				tcp_sendreset(tcpheader, &key, &ipv4);

				continue;
			}
		} else {
			__assert(!"Bad task type");
		}

		MUTEX_ACQUIRE(&tcb->mutex, false);

		if (tasktype == TASK_TYPE_TIMEOUT && tcb->state != TCB_STATE_CLOSED && tcb->state != TCB_STATE_ABORT) {
			tcp_handletimeout(tcb);
			MUTEX_RELEASE(&tcb->mutex);
			TCB_RELEASE(tcb);
			continue;
		}

		// TODO validade reset if in sequence window (rfc 793 page 37)
		if (tcpheader->seq == tcb->rcvnext && (tcpheader->control & CONTROL_RST) && tcb->state != TCB_STATE_LISTEN) {
			tcp_reset(tcb);
			MUTEX_RELEASE(&tcb->mutex);
			TCB_RELEASE(tcb);
			continue;
		}

		__assert(tcb->state != TCB_STATE_CLOSED);
		switch (tcb->state) {
			case TCB_STATE_LISTEN: {
				// (listening socket only)
				// only packets with only a SYN will come here
				// allocate a new TCB with state SYNRECEIVED and
				// send back a SYNACK for it. use the SYN RTO
				// TODO data could also be sent here

				// first check if a connection was already handled by another thread, if so discard this packet
				connkey_t key = {
					.peer = ipv4.source,
					.peerport = tcpheader->srcport,
					.localport = tcpheader->dstport
				};

				tcb_t *testtcb = tcbget(&key, false);
				if (testtcb) {
					TCB_RELEASE(testtcb);
					break;
				}

				uint32_t mtu = ipv4_getmtu(ipv4.source);
				uint32_t self = ipv4_getnetdevip(ipv4.source);
				// if mtu or self is 0 the ip tables have been updated
				// and this conenction would die out anyways
				if (mtu == 0 || self == 0)
					break;

				// no tcb for connection key, allocate one and set up initial state
				tcb_t *newtcb = allocatetcb(mtu);
				if (newtcb == NULL) {
					// out of memory to allocate tcb,
					// ignore this packet and hope it gets retransmitted later.
					break;
				}

				MUTEX_ACQUIRE(&newtcb->mutex, false);
				newtcb->state = TCB_STATE_SYNRECEIVED;
				newtcb->parent = tcb;
				TCB_HOLD(tcb); // for parent ref
				newtcb->key = key;
				newtcb->sndmss = mtu;
				newtcb->sndmss = getsendmss(tcpheader, newtcb);
				newtcb->iss = getrand32();
				newtcb->rcvwindow = 0;
				newtcb->sndnext = newtcb->iss;
				newtcb->rcvmss = MSS_LIMIT(mtu);
				newtcb->rcvnext = tcpheader->seq + 1;
				newtcb->rcvwindow = tcpheader->window;
				newtcb->rcvurgent = 0;
				newtcb->irs = tcpheader->seq;

				// add tcb to the connection table
				if (tcbset(newtcb, &key, false)) {
					// free tcb and wait for a retransmission
					TCB_RELEASE(newtcb);
					break;
				}

				// make sure we will have enough backlog space for it
				if (tcb->backlogfree == 0) {
					// unset the tcb and wait for a SYN retransmission
					newtcb->state = TCB_STATE_ABORT;
					tcbset(newtcb, &key, true);
					TCB_RELEASE(newtcb);
					break;
				}

				// actually send the packet now, having it stay in the transmit buffer
				synoptions_t synoptions = {
					.msskind = OPTIONS_MSS_KIND,
					.msslen = OPTIONS_MSS_LEN,
					.mss = newtcb->rcvmss
				};

				tcb->retransmitpacketlen = sizeof(tcpheader_t) + sizeof(synoptions_t);
				tcp_createheader(newtcb->retransmitbuffer, newtcb, tcb->retransmitpacketlen, &synoptions, sizeof(synoptions_t), CONTROL_SYN | CONTROL_ACK, 0);

				// ugly hack but whatever, we only add options here and in connect.
				synoptions_t *fixptr = (synoptions_t *)((uintptr_t)newtcb->retransmitbuffer + sizeof(tcpheader_t));
				fixptr->mss = cpu_to_be_w(fixptr->mss);

				newtcb->sndunack = newtcb->sndnext;
				newtcb->sndnext += 1;
				if (tcp_sendpacket(newtcb->retransmitbuffer, tcb->retransmitpacketlen, key.peer, self)) {
					// unset the tcb and wait for a SYN retransmission
					tcp_reset(tcb);
					TCB_RELEASE(newtcb);
					break;
				}

				// allocate space in the backlog for it
				--tcb->backlogfree;

				MUTEX_RELEASE(&newtcb->mutex);
				break;
			}
			case TCB_STATE_SYNSENT: {
				// (connect() socket only)
				// for sockets which called tcp_connect.
				// we have sent a SYN so now wait for a SYNACK from the specific peer.
				// once received, the connection has been stabilished and we send back an ACK.
				// use the SYN RTO.
				// TODO data could also be sent here
				if ((tcpheader->control & (CONTROL_SYN | CONTROL_ACK)) != (CONTROL_SYN | CONTROL_ACK) || tcb->sndunack + 1 != tcpheader->ack) {
					tcp_sendreset(tcpheader, &tcb->key, &ipv4);
					break;
				}

				// get data about the peer and send back an ACK.
				tcb->sndunack = tcb->sndnext;
				itimer_pause(&tcb->itimer, NULL, NULL);
				tcb->state = TCB_STATE_ESTABILISHED;
				tcb->sndwindow = tcpheader->window;
				tcb->sndseqwl = tcpheader->seq;
				tcb->sndackwl = tcpheader->ack;
				tcb->irs = tcpheader->seq;
				tcb->rcvwindow = TCB_RINGBUFFER_SIZE;
				tcb->rcvnext = tcb->irs + 1;
				tcb->sndmss = getsendmss(tcpheader, tcb);

				tcp_ack(tcb);
				poll_event(&tcb->pollheader, POLLOUT);
				break;
			}
			case TCB_STATE_SYNRECEIVED: {
				// (listening socket child only)
				// waiting for an ACK on our SYNACK.
				// use the SYN RTO
				// TODO data could also be sent here

				// SYN retransmission, just retransmit the data in the transmit buffeer
				if (((tcpheader->control & CONTROL_SYN) && (tcpheader->control & CONTROL_ACK) == 0)) {
					tcp_retransmit(tcb);
					break;
				}

				if ((tcpheader->control & CONTROL_ACK) && tcb->sndunack + 1 == tcpheader->ack) {
					itimer_pause(&tcb->itimer, NULL, NULL);
					tcb->state = TCB_STATE_ESTABILISHED;
					tcb->sndunack += 1;
					MUTEX_ACQUIRE(&tcb->parent->mutex, false);
					if (tcb->parent->state == TCB_STATE_CLOSED) {
						// listening socket was closed in the mean time. close the connection
						tcp_handleclose(tcb);
					} else {
						// space was already allocated for this in the TCB_STATE_LISTEN handling
						__assert(ringbuffer_write(&tcb->parent->backlog, &tcb, sizeof(tcb_t *)) == sizeof(tcb_t *));
						poll_event(&tcb->parent->pollheader, POLLIN);
					}
					MUTEX_RELEASE(&tcb->parent->mutex);
				} else {
					tcp_sendreset(tcpheader, &tcb->key, &ipv4);
				}
				break;
			}
			case TCB_STATE_ESTABILISHED: {
				// Data can now be exchanged properly.
				// Handle incoming packets as data
				// In case packet has a FIN on the right sequence,
				// send an ACK and switch state to TCB_STATE_CLOSEWAIT
				// use the data RTO

				// SYNACK retransmission
				if ((tcpheader->control & CONTROL_SYN) && (tcpheader->control & CONTROL_ACK)) {
					tcp_ack(tcb);
					break;
				}

				tcp_handledatareceive(tcb, tcpheader, &ipv4, TCB_STATE_CLOSEWAIT);
				break;
			}

			case TCB_STATE_CLOSEWAIT: {
				// We have received a FIN from the peer and now we know we won't receive any more data.
				// We are just waiting for the socket to be closed by the local side. In this state, we will
				// either be (re)transmitting data or a FIN request.
				// use the data RTO.

				// retransmitted fin from peer handled here
				if (tcpheader->control & CONTROL_FIN) {
					tcp_ack(tcb);
					break;
				}

				if ((tcpheader->control & CONTROL_ACK) && tcb->sndnext == tcpheader->ack) {
					// some packet has been acked!
					tcp_handleack(tcb, tcpheader);
				}
				break;
			}
			case TCB_STATE_LASTACK: {
				// The peer has closed their end of the connection and we
				// have sent a FIN request. Wait for an ACK. Once received,
				// close the connection and free the tcb.
				// Use the FIN RTO

				if ((tcpheader->control & CONTROL_ACK) && tcb->sndnext + 1 == tcpheader->ack) {
					// we have been acked, the connection has been closed.
					// remove the TCB from the connection table
					tcbset(tcb, &tcb->key, true);
					itimer_pause(&tcb->itimer, NULL, NULL);
					tcb->state = TCB_STATE_CLOSED;
					break;
				}
				break;
			}

			case TCB_STATE_FINWAIT1: {
				// the connection has been closed locally and a FIN has been sent.
				// theres no more data in the transmit buffer too.
				// wait for either a FIN or an ACK of our FIN.
				// if we get a FIN, it will be handled in tcp_handletadareceive() and will go to TCB_STATE_CLOSING
				// if we get an ACK of our FIN, go to state TCB_STATE_FINWAIT2
				// if we get both, it will skip straight to TCB_STATE_TIMEWAIT.
				// Use the FIN RTO

				if ((tcpheader->control & CONTROL_ACK) && tcb->sndnext + 1 == tcpheader->ack) {
					tcb->sndnext += 1;
					tcb->state = TCB_STATE_FINWAIT2;
				}

				tcp_handledatareceive(tcb, tcpheader, &ipv4, tcb->state == TCB_STATE_FINWAIT2 ? TCB_STATE_TIMEWAIT : TCB_STATE_CLOSING);
				if (tcb->state == TCB_STATE_TIMEWAIT) {
					itimer_pause(&tcb->itimer, NULL, NULL);
					itimer_set(&tcb->itimer, MSL_SEC * 1000000, 0);
					itimer_resume(&tcb->itimer);
				}
				break;
			}

			case TCB_STATE_FINWAIT2: {
				// the connection has been closed locally and our FIN has been acknowledged.
				// wait until we receive a FIN and send back an ACK and go to state TCB_STATE_TIMEWAIT
				// No retransmission is done here, its simply a waiting state.

				__assert(tasktype == TASK_TYPE_PACKET);
				tcp_handledatareceive(tcb, tcpheader, &ipv4, TCB_STATE_TIMEWAIT);
				if (tcb->state == TCB_STATE_TIMEWAIT) {
					itimer_pause(&tcb->itimer, NULL, NULL);
					itimer_set(&tcb->itimer, MSL_SEC * 1000000, 0);
					itimer_resume(&tcb->itimer);
				}
				break;
			}
			case TCB_STATE_CLOSING: {
				// we sent a FIN and received a FIN before our FIN was acknowledged.
				// Wait for an ACK of our FIN and retransmit it as nescessary.
				// Also listen in case the peer retransmits their FIN.
				// Use the FIN RTO.

				if (tcpheader->control & CONTROL_FIN) {
					// peer retransmission
					tcp_ack(tcb);
				}

				if ((tcpheader->control & CONTROL_ACK) && tcb->sndnext + 1 == tcpheader->ack) {
					tcb->sndnext += 1;
					tcb->state = TCB_STATE_TIMEWAIT;
					itimer_pause(&tcb->itimer, NULL, NULL);
					itimer_set(&tcb->itimer, MSL_SEC * 1000000, 0);
					itimer_resume(&tcb->itimer);
				}
				break;
			}
			case TCB_STATE_TIMEWAIT: {
				// we have sent a FIN, had it ACKed, received a FIN and sent back an ACK.
				// here, we will wait 2MSL in case the peer retransmits the FIN.
				// No retransmission is done here, we simply wait either for the timeout or for a FIN to ACK.
				// got a FIN retransmission, ack it.
				tcp_ack(tcb);
				break;
			}
			default:
		}
		MUTEX_RELEASE(&tcb->mutex);
		TCB_RELEASE(tcb);
	}
}

static int internalpoll(tcb_t *tcb, polldata_t* data, int events) {
	int revents = 0;
	if (tcb->reset)
		revents |= POLLERR;

	switch (tcb->state) {
		case TCB_STATE_LISTEN:
			if (RINGBUFFER_DATACOUNT(&tcb->backlog))
				revents |= POLLIN & events;
			break;
		case TCB_STATE_SYNSENT:
		case TCB_STATE_SYNRECEIVED:
			// nothing to do for now, waiting for connection
			break;
		// both ends are open
		case TCB_STATE_ESTABILISHED:
			if (RINGBUFFER_DATACOUNT(&tcb->transmitbuffer) != TCB_RINGBUFFER_SIZE)
				revents |= POLLOUT & events;
		// here only our end is closed
		case TCB_STATE_FINWAIT1:
		case TCB_STATE_FINWAIT2:
			if (RINGBUFFER_DATACOUNT(&tcb->receivebuffer))
				revents |= POLLIN & events;
			// TODO POLLPRI
			break;
		// here the other end is closed 
		case TCB_STATE_CLOSEWAIT:
		case TCB_STATE_CLOSING:
		case TCB_STATE_LASTACK:
		case TCB_STATE_TIMEWAIT:
		case TCB_STATE_CLOSED:
			// waiting for local side to close socket
			if (RINGBUFFER_DATACOUNT(&tcb->receivebuffer))
				revents |= POLLIN & events;
			revents |= POLLHUP;
			break;
	}

	if (data && revents == 0)
		poll_add(&tcb->pollheader, data, events);

	return revents;
}

// TODO buffer up segments instead of instantly sending them
static int tcp_send(socket_t *socket, sockaddr_t *addr, void *buffer, size_t count, uintmax_t flags, size_t *sendcount) {
	if (addr)
		return ENOTCONN;

	tcpsocket_t *tcpsocket = (tcpsocket_t *)socket;
	MUTEX_ACQUIRE(&socket->mutex, false);
	tcb_t *tcb = tcpsocket->tcb;
	int error = 0;
	if (tcb == NULL) {
		MUTEX_RELEASE(&socket->mutex);
		return ENOTCONN;
	}

	MUTEX_ACQUIRE(&tcb->mutex, false);
	if (tcb->reset) {
		if (_cpu()->thread->proc)
			signal_signalproc(_cpu()->thread->proc, SIGPIPE);
		error = ECONNRESET;
		goto leave;
	}

	// wait until we can send data
	for (;;) {
		polldesc_t polldesc = {0};
		error = poll_initdesc(&polldesc, 1);
		if (error)
			goto leave;

		int revents = internalpoll(tcb, &polldesc.data[0], POLLOUT);
		if (revents & POLLHUP) {
			poll_leave(&polldesc);
			poll_destroydesc(&polldesc);

			if (_cpu()->thread->proc)
				signal_signalproc(_cpu()->thread->proc, SIGPIPE);

			error = EPIPE;
			goto leave;
		}

		if (revents & POLLOUT) {
			poll_leave(&polldesc);
			poll_destroydesc(&polldesc);
			break;
		}

		if (revents & V_FFLAGS_NONBLOCKING) {
			poll_leave(&polldesc);
			poll_destroydesc(&polldesc);
			error = EAGAIN;
			goto leave;
		}

		MUTEX_RELEASE(&tcb->mutex);
		int error = poll_dowait(&polldesc, 0);
		poll_leave(&polldesc);
		poll_destroydesc(&polldesc);
		MUTEX_ACQUIRE(&tcb->mutex, false);

		if (error)
			goto leave;
	}

	size_t writesize = min(TCB_RINGBUFFER_SIZE - RINGBUFFER_DATACOUNT(&tcb->transmitbuffer), count);
	__assert(writesize);

	__assert(ringbuffer_write(&tcb->transmitbuffer, buffer, writesize) == writesize);
	*sendcount = writesize;

	if (tcb->sndnext == tcb->sndunack)
		error = tcp_transmitnextsegment(tcb);

	leave:
	MUTEX_RELEASE(&tcb->mutex);
	MUTEX_RELEASE(&socket->mutex);
	return error;
}

static int tcp_recv(socket_t *socket, sockaddr_t *addr, void *buffer, size_t count, uintmax_t flags, size_t *recvcount) {
	tcpsocket_t *tcpsocket = (tcpsocket_t *)socket;
	int error = 0;
	MUTEX_ACQUIRE(&socket->mutex, false);
	tcb_t *tcb = tcpsocket->tcb;
	if (tcb == NULL) {
		MUTEX_RELEASE(&socket->mutex);
		return ENOTCONN;
	}
	MUTEX_ACQUIRE(&tcb->mutex, false);
	if (tcb->state == TCB_STATE_CLOSED || tcb->state == TCB_STATE_SYNSENT || tcb->state == TCB_STATE_SYNRECEIVED) {
		error = ENOTCONN;
		goto cleanup;
	}

	if (tcb->reset) {
		error = ECONNRESET;
		goto cleanup;
	}

	for (;;) {
		polldesc_t desc = {0};
		error = poll_initdesc(&desc, 1);
		if (error)
			goto cleanup;

		int revents = internalpoll(tcb, &desc.data[0], POLLIN);

		if (revents) {
			poll_leave(&desc);
			poll_destroydesc(&desc);
			break;
		}

		if (flags & V_FFLAGS_NONBLOCKING) {
			error = EAGAIN;
			poll_leave(&desc);
			poll_destroydesc(&desc);
			goto cleanup;
		}

		MUTEX_RELEASE(&tcb->mutex);

		error = poll_dowait(&desc, 0);

		poll_leave(&desc);
		poll_destroydesc(&desc);
		MUTEX_ACQUIRE(&tcb->mutex, false);

		if (error)
			goto cleanup;
	}

	size_t copycount = min(RINGBUFFER_DATACOUNT(&tcb->receivebuffer), count);
	if (flags & SOCKET_RECV_FLAGS_PEEK) {
		__assert(ringbuffer_peek(&tcb->receivebuffer, buffer, 0, copycount) == copycount);
	} else {
		__assert(ringbuffer_read(&tcb->receivebuffer, buffer, copycount) == copycount);
		tcb->rcvwindow += copycount;
		// notify about the window increase
		tcp_ack(tcb);
	}
	*recvcount = copycount;

	cleanup:
	MUTEX_RELEASE(&tcb->mutex);
	MUTEX_RELEASE(&socket->mutex);
	return error;
}

static int tcp_poll(socket_t *socket, polldata_t *data, int events) {
	tcpsocket_t *tcpsocket = (tcpsocket_t *)socket;
	MUTEX_ACQUIRE(&socket->mutex, false);
	int revents = 0;
	if (tcpsocket->tcb == NULL) {
		// socket has never connected
		revents = POLLERR;
		goto leave;
	}

	MUTEX_ACQUIRE(&tcpsocket->tcb->mutex, false);

	revents = internalpoll(tcpsocket->tcb, data, events);

	MUTEX_RELEASE(&tcpsocket->tcb->mutex);
	leave:
	MUTEX_RELEASE(&socket->mutex);
	return revents;
}

static int tcp_bind(socket_t *socket, sockaddr_t *addr) {
	tcpsocket_t *tcpsocket = (tcpsocket_t *)socket;
	__assert(addr->ipv4addr.addr == 0);
	MUTEX_ACQUIRE(&socket->mutex, false);
	tcb_t *tcb = tcpsocket->tcb;
	if (tcb == NULL) {
		tcb = allocatetcb(1); // smallest possible buffers TODO not even allocate those
		if (tcb == NULL) {
			MUTEX_RELEASE(&socket->mutex);
			return ENOMEM;
		}
	} else {
		TCB_HOLD(tcb); // for the release later
	}

	MUTEX_ACQUIRE(&tcb->mutex, false);

	int error;
	if (tcb->state != TCB_STATE_CLOSED || tcb->port) {
		error = EINVAL;
		goto cleanup;
	}

	tcb->port = addr->ipv4addr.port;
	error = allocateport(&tcb->port);
	if (error)
		goto cleanup;

	if (tcpsocket->tcb == NULL) {
		tcpsocket->tcb = tcb;
		TCB_HOLD(tcb);
	}

	addr->ipv4addr.port = tcb->port;

	cleanup:
	MUTEX_RELEASE(&tcb->mutex);
	MUTEX_RELEASE(&socket->mutex);
	TCB_RELEASE(tcb);
	return error;
}

static int tcp_connect(socket_t *socket, sockaddr_t *addr, uintmax_t flags) {
	tcpsocket_t *tcpsocket = (tcpsocket_t *)socket;

	size_t mtu = ipv4_getmtu(addr->ipv4addr.addr);
	uint32_t self = ipv4_getnetdevip(addr->ipv4addr.addr);
	if (mtu == 0 || self == 0)
		return ENETUNREACH; // no route to ip in routing table

	MUTEX_ACQUIRE(&socket->mutex, false);
	tcb_t *tcb = tcpsocket->tcb;
	if (tcb == NULL) {
		tcb = allocatetcb(mtu);
		if (tcb == NULL) {
			MUTEX_RELEASE(&socket->mutex);
			return ENOMEM;
		}
	} else {
		TCB_HOLD(tcb); // for the release later
	}


	if (tcpsocket->tcb && tcpsocket->tcb->state == TCB_STATE_SYNSENT) {
		TCB_RELEASE(tcb);
		MUTEX_RELEASE(&socket->mutex);
		return EALREADY;
	}

	if (tcpsocket->tcb && tcpsocket->tcb->state != TCB_STATE_CLOSED) {
		TCB_RELEASE(tcb);
		MUTEX_RELEASE(&socket->mutex);
		return EISCONN;
	}

	MUTEX_ACQUIRE(&tcb->mutex, false);

	polldesc_t polldesc = {0};
	int error = poll_initdesc(&polldesc, 1);
	if (error)
		goto cleanup_nopoll;

	error = 0;
	if (tcb->port == 0) {
		error = allocateport(&tcb->port);
		if (error)
			goto cleanup;
	}

	connkey_t key = {
		.peer = addr->ipv4addr.addr,
		.peerport = addr->ipv4addr.port,
		.localport = tcb->port
	};

	error = tcbset(tcb, &key, false);
	if (error)
		goto cleanup;

	tcb->iss = getrand32();
	tcb->rcvwindow = 0;
	tcb->sndnext = tcb->iss;
	tcb->sndunack = tcb->iss;
	tcb->key = key;
	tcb->rcvmss = MSS_LIMIT(mtu);
	tcb->sndmss = mtu;

	synoptions_t synoptions = {
		.msskind = OPTIONS_MSS_KIND,
		.msslen = OPTIONS_MSS_LEN,
		.mss = tcb->rcvmss
	};

	tcb->retransmitpacketlen = sizeof(tcpheader_t) + sizeof(synoptions_t);
	tcp_createheader(tcb->retransmitbuffer, tcb, tcb->retransmitpacketlen, &synoptions, sizeof(synoptions_t), CONTROL_SYN, 0);

	// ugly hack but whatever, we only add options here and in the other SYN response
	synoptions_t *fixptr = (synoptions_t *)((uintptr_t)tcb->retransmitbuffer + sizeof(tcpheader_t));
	fixptr->mss = cpu_to_be_w(fixptr->mss);

	tcb->sndnext += 1;
	tcb->state = TCB_STATE_SYNSENT;
	error = tcp_sendpacket(tcb->retransmitbuffer, tcb->retransmitpacketlen, key.peer, self);
	if (error) {
		tcb->state = TCB_STATE_CLOSED;
		tcbset(tcb, &key, true);
		goto cleanup;
	}

	tcb->currentrto = RTO_START_SEC;
	tcb->lastsend = timekeeper_timefromboot();
	itimer_set(&tcb->itimer, RTO_START_SEC * 1000000, 0);
	itimer_resume(&tcb->itimer);

	if (tcpsocket->tcb == NULL) {
		TCB_HOLD(tcb);
		tcpsocket->tcb = tcb;
	}

	if (flags & V_FFLAGS_NONBLOCKING) {
		error = EINPROGRESS;
		goto cleanup;
	}

	poll_add(&tcb->pollheader, &polldesc.data[0], POLLOUT);
	MUTEX_RELEASE(&tcb->mutex);
	error = poll_dowait(&polldesc, 0);
	if (polldesc.event && polldesc.event->revents & POLLERR)
		error = ECONNREFUSED;

	poll_leave(&polldesc);
	poll_destroydesc(&polldesc);

	goto cleanup_notcbmutex;

	cleanup:
	poll_leave(&polldesc);
	poll_destroydesc(&polldesc);
	cleanup_nopoll:
	MUTEX_RELEASE(&tcb->mutex);
	cleanup_notcbmutex:
	TCB_RELEASE(tcb);
	MUTEX_RELEASE(&socket->mutex);
	return error;
}

static int tcp_listen(socket_t *socket, int backlog) {
	tcpsocket_t *tcpsocket = (tcpsocket_t *)socket;
	MUTEX_ACQUIRE(&socket->mutex, false);
	tcb_t *tcb = tcpsocket->tcb;
	if (tcb == NULL) {
		tcb = allocatetcb(1); // smallest possible buffers TODO not even allocate those
		if (tcb == NULL) {
			MUTEX_RELEASE(&socket->mutex);
			return ENOMEM;
		}
	} else {
		TCB_HOLD(tcb); // for the release later
	}

	int error;
	if (tcb->state != TCB_STATE_CLOSED) {
		error = EINVAL;
		goto cleanup;
	}

	// allocate ephemeral port if unbound
	if (tcb->port == 0) {
		error = allocateport(&tcb->port);
		if (error)
			goto cleanup;
	}

	// allocate backlog
	error = ringbuffer_init(&tcb->backlog, backlog * sizeof(tcb_t *));
	if (error)
		goto cleanup;

	connkey_t key = {
		.localport = tcb->port,
		.peerport = 0,
		.peer = 0
	};

	error = tcbset(tcb, &key, false);
	if (error) {
		ringbuffer_destroy(&tcb->backlog);
		goto cleanup;
	}

	tcb->state = TCB_STATE_LISTEN;
	tcb->backlogfree = backlog;

	if (tcpsocket->tcb == NULL) {
		tcpsocket->tcb = tcb;
		TCB_HOLD(tcb);
	}

	cleanup:
	MUTEX_RELEASE(&tcb->mutex);
	MUTEX_RELEASE(&socket->mutex);
	TCB_RELEASE(tcb);
	return error;
}

static int tcp_accept(socket_t *server, socket_t *client, sockaddr_t *addr, uintmax_t flags) {
	tcpsocket_t *tcpserver = (tcpsocket_t *)server;
	tcpsocket_t *tcpclient = (tcpsocket_t *)client;
	// no need to lock client, only one thread is supposed to have a reference to it right now.
	MUTEX_ACQUIRE(&server->mutex, false);

	tcb_t *tcb = tcpserver->tcb;
	int error = 0;
	if (tcb == NULL) {
		MUTEX_RELEASE(&server->mutex);
		return EINVAL;
	}

	MUTEX_ACQUIRE(&tcb->mutex, false);

	if (tcb->state != TCB_STATE_LISTEN) {
		error = EINVAL;
		goto cleanup;
	}

	// it is a listening socket
	for (;;) {
		polldesc_t desc = {0};
		error = poll_initdesc(&desc, 1);
		if (error)
			goto cleanup;

		int revents = internalpoll(tcb, &desc.data[0], POLLIN);
		if (revents) {
			poll_leave(&desc);
			poll_destroydesc(&desc);
			break;
		}

		if (flags & V_FFLAGS_NONBLOCKING) {
			error = EAGAIN;
			poll_leave(&desc);
			poll_destroydesc(&desc);
			goto cleanup;
		}

		MUTEX_RELEASE(&tcb->mutex);

		error = poll_dowait(&desc, 0);

		poll_leave(&desc);
		poll_destroydesc(&desc);
		MUTEX_ACQUIRE(&tcb->mutex, false);

		if (error)
			goto cleanup;
	}

	// there is a tcb on the backlog
	tcb_t *newtcb;
	__assert(ringbuffer_read(&tcb->backlog, &newtcb, sizeof(tcb_t *)) == sizeof(tcb_t *));

	++tcb->backlogfree;

	// pass the reference from the backlog to the new socket
	tcpclient->tcb = newtcb;
	if (newtcb->reset)
		error = ECONNABORTED;

	cleanup:
	MUTEX_RELEASE(&tcb->mutex);
	MUTEX_RELEASE(&server->mutex);
	return error;
}

static size_t tcp_datacount(socket_t *socket) {
	tcpsocket_t *tcpsocket = (tcpsocket_t *)socket;
	MUTEX_ACQUIRE(&socket->mutex, false);
	size_t count = 0;
	if (tcpsocket->tcb == NULL)
		goto cleanup;

	MUTEX_ACQUIRE(&tcpsocket->tcb->mutex, false);

	count = RINGBUFFER_DATACOUNT(&tcpsocket->tcb->receivebuffer);

	cleanup:
	MUTEX_RELEASE(&tcpsocket->tcb->mutex);
	MUTEX_RELEASE(&socket->mutex);
	return count;
}

static void tcp_destroy(socket_t *socket) {
	tcpsocket_t *tcpsocket = (tcpsocket_t *)socket;
	// closing the socket
	// no need to acquire the socket mutex as we have the last reference to it
	tcb_t *tcb = tcpsocket->tcb;
	if (tcb) {
		MUTEX_ACQUIRE(&tcb->mutex, false);

		if (tcb->state != TCB_STATE_CLOSED && tcb->state != TCB_STATE_ABORT)
			tcp_handleclose(tcb);

		MUTEX_RELEASE(&tcb->mutex);
		TCB_RELEASE(tcb);
	}

	free(socket);
}

static socketops_t socketops = {
	.bind = tcp_bind,
	.send = tcp_send,
	.recv = tcp_recv,
	.poll = tcp_poll,
	.connect = tcp_connect,
	.listen = tcp_listen,
	.accept = tcp_accept,
	.datacount = tcp_datacount,
	.destroy = tcp_destroy
};

socket_t *tcp_createsocket() {
	tcpsocket_t *socket = alloc(sizeof(tcpsocket_t));
	if (socket == NULL)
		return NULL;

	socket->socket.ops = &socketops;

	return (socket_t *)socket;
}

void tcp_init() {
	MUTEX_INIT(&conntablemutex);
	SPINLOCK_INIT(portlock);
	__assert(hashtable_init(&conntable, 1000) == 0);
	for (int i = 0; i < WORKER_COUNT; ++i) {
		// initialize worker threads

		SPINLOCK_INIT(workers[i].lock);
		SEMAPHORE_INIT(&workers[i].semaphore, 0);
		__assert(ringbuffer_init(&workers[i].ringbuffer, WORKER_BUFFER_SIZE) == 0);
		thread_t *thread = sched_newthread(tcp_worker, PAGE_SIZE * 3, 0, NULL, NULL);
		__assert(thread);
		thread->kernelarg = &workers[i];
		sched_queue(thread);
	}
}
