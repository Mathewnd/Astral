#include <kernel/net.h>
#include <hashtable.h>
#include <kernel/scheduler.h>
#include <kernel/timekeeper.h>
#include <logging.h>
#include <kernel/slab.h>
#include <kernel/eth.h>
#include <ringbuffer.h>

#define CACHE_TIMEOUT_SEC 300
#define CACHE_TIMEOUT_MS  (CACHE_TIMEOUT_SEC * 1000)
#define LOOKUP_TIMEOUT_SEC 5
#define OPCODE_REQUEST 1
#define OPCODE_REPLY 2

typedef struct {
	mac_t mac;
	timespec_t time;
} entry_t;
typedef struct {
	netdev_t *netdev;
	uint32_t ip;
} key_t;

static scache_t *entryallocator;

static thread_t *handlerthread;

static thread_t *cleanupthread;

static mutex_t cachelock;
static hashtable_t caches;

static int get(netdev_t *netdev, uint32_t ip, mac_t *mac) {
	void *ptr;
	int e = hashtable_get(&netdev->arpcache, &ptr, &ip, sizeof(ip));
	entry_t *entry = ptr;
	if (e == 0)
		*mac = entry->mac;

	return e;
}

static void set(netdev_t *netdev, uint32_t ip, mac_t mac, timespec_t time) {
	// set cache into the cache list if not already there
	void *cachetmp;
	if (hashtable_get(&caches, &cachetmp, &netdev, sizeof(netdev))) {
		// if it can't be set, give up as it would not be able to be cleaned up later
		if (hashtable_set(&caches, &netdev->arpcache, &netdev, sizeof(netdev), true))
			return;
	}

	mac_t tmp;
	// already exists, just signal to the waiting threads
	if (get(netdev, ip, &tmp) == 0) {
		return;
	}

	entry_t *entry = slab_allocate(entryallocator);
	// the thread waiting for the lookup will just timeout if the system is out of memory. not ideal but meh
	if (entry == NULL)
		return;

	entry->mac = mac;
	entry->time = time;

	// same here, waiting thread will just timeout if set errors out
	if (hashtable_set(&netdev->arpcache, entry, &ip, sizeof(ip), true)) {
		slab_free(entryallocator, entry);
		return;
	}
}

static void cleanupthreadfn() {
	for (;;) {
		sched_sleepus(1000000); // 1 second
		MUTEX_ACQUIRE(&cachelock, false);
		timespec_t currtime = timekeeper_time();
		hashentry_t entrybuffer;
		HASHTABLE_FOREACH(&caches) {
			hashtable_t *arpcache = entry->value;

			HASHTABLE_FOREACH(arpcache) {
				entry_t *cacheentry = entry->value;
				if (timespec_diffms(currtime, cacheentry->time) < CACHE_TIMEOUT_MS)
					continue;

				// swap the current entry pointer (that is in the hashtable) to a local copy so we can keep the next pointer
				// without breaking the loop by using released memory
				// hacky solution but :P
				entrybuffer = *entry;
				entry = &entrybuffer;
				hashtable_remove(arpcache, entry->key, entry->keysize);
				slab_free(entryallocator, cacheentry);
			}
		}
		MUTEX_RELEASE(&cachelock);
	}
}

typedef struct {
	uint16_t hwtype;
	uint16_t protocoltype;
	uint8_t hwlen;
	uint8_t ptlen;
	uint16_t opcode;
	mac_t srchw;
	uint32_t srcpt;
	mac_t dsthw;
	uint32_t dstpt;
} __attribute__((packed)) arpframe_t;

static ringbuffer_t processbuffer;
static semaphore_t psem;
static spinlock_t ringlock;

typedef struct {
	int opcode;
	netdev_t *netdev;
	uint32_t ip;
	mac_t mac;
} bufferentry_t;

static void send_reply(netdev_t *netdev, uint32_t ip, mac_t mac) {
	arpframe_t frame = {
		.hwtype = cpu_to_be_w(1),
		.protocoltype = cpu_to_be_w(0x800),
		.hwlen = 6,
		.ptlen = 4,
		.opcode = cpu_to_be_w(OPCODE_REPLY),
		.srcpt = cpu_to_be_d(netdev->ip),
		.dstpt = cpu_to_be_d(ip)
	};

	memcpy(&frame.srchw, &netdev->mac, sizeof(mac_t));
	memcpy(&frame.dsthw, &mac, sizeof(mac_t));
	netdesc_t desc;
	int e = netdev->allocdesc(netdev, sizeof(arpframe_t), &desc);
	if (e)
		return;

	memcpy((void *)((uintptr_t)desc.address + desc.curroffset), &frame, sizeof(frame));

	e = netdev->sendpacket(netdev, desc, mac, ETH_PROTO_ARP);
	netdev->freedesc(netdev, &desc);
}

static void handlerthreadfn() {
	for (;;) {
		semaphore_wait(&psem, false);
		bufferentry_t entry;
		int old = interrupt_raiseipl(IPL_DPC);
		spinlock_acquire(&ringlock);

		ringbuffer_read(&processbuffer, &entry, sizeof(entry));

		spinlock_release(&ringlock);
		interrupt_loweripl(old);

		switch (entry.opcode) {
			case OPCODE_REPLY:
				// add to cache
				MUTEX_ACQUIRE(&cachelock, false);
				timespec_t time = timekeeper_time();
				set(entry.netdev, entry.ip, entry.mac, time);
				MUTEX_RELEASE(&cachelock);
				break;
			case OPCODE_REQUEST:
				// reply
				send_reply(entry.netdev, entry.ip, entry.mac);
		}
	}
}

// executed in dpc context
void arp_process(netdev_t *netdev, void *buffer) {
	arpframe_t *frame = buffer;
	if (frame->hwtype != cpu_to_be_w(1)) // ethernet
		return;

	if (frame->protocoltype != cpu_to_be_w(0x800)) // ip
		return;

	if (frame->hwlen != 6)
		return;

	if (frame->ptlen != 4)
		return;

	if (be_to_cpu_w(frame->opcode) == OPCODE_REPLY && MAC_EQUAL(&frame->dsthw, &netdev->mac) == false)
		return;

	if (be_to_cpu_w(frame->opcode) == OPCODE_REQUEST && be_to_cpu_d(frame->dstpt) != netdev->ip)
		return;

	bufferentry_t entry = {
		.opcode = cpu_to_be_w(frame->opcode),
		.netdev = netdev,
		.ip = be_to_cpu_d(frame->srcpt),
		.mac = frame->srchw
	};

	spinlock_acquire(&ringlock);
	ringbuffer_write(&processbuffer, &entry, sizeof(entry));
	spinlock_release(&ringlock);
	semaphore_signal(&psem);
}

static int sendrequest(netdev_t *netdev, uint32_t ip) {
	arpframe_t frame = {
		.hwtype = cpu_to_be_w(1),
		.protocoltype = cpu_to_be_w(0x800),
		.hwlen = 6,
		.ptlen = 4,
		.opcode = cpu_to_be_w(OPCODE_REQUEST),
		.srcpt = 0,
		.dsthw = (mac_t){{0,0,0,0,0,0}},
		.dstpt = cpu_to_be_d(ip)
	};

	memcpy(&frame.srchw, &netdev->mac, sizeof(mac_t));
	netdesc_t desc;
	int e = netdev->allocdesc(netdev, sizeof(arpframe_t), &desc);
	if (e)
		return e;

	memcpy((void *)((uintptr_t)desc.address + desc.curroffset), &frame, sizeof(frame));

	e = netdev->sendpacket(netdev, desc, NET_BROADCAST_MAC, ETH_PROTO_ARP);
	netdev->freedesc(netdev, &desc);
	return e;
}

int arp_lookup(netdev_t *netdev, uint32_t ip, mac_t *mac) {
	MUTEX_ACQUIRE(&cachelock, false);
	int e = 0;
	if (get(netdev, ip, mac) == 0)
		goto cleanup;

	// not found, request it
	e = sendrequest(netdev, ip);
	if (e)
		goto cleanup;

	// wait
	// TODO wait on an event instead of looping on a yield (ok for now, but might cause slowdows in the future)

	timespec_t end = timekeeper_time();
	//end.s += LOOKUP_TIMEOUT_SEC;
	timespec_t other;
	other.s = 0;
	other.ns = 100000000;
	end = timespec_add(end, other);

	for (;;) {
		MUTEX_RELEASE(&cachelock);
		sched_yield();
		MUTEX_ACQUIRE(&cachelock, true);

		timespec_t time = timekeeper_time();
		if (get(netdev, ip, mac) == 0) {
			e = 0;
			break;
		}

		if (time.s > end.s || (end.s == time.s && time.ns >= end.ns)) {
			e = ENETUNREACH;
			break;
		}
	}
	cleanup:
	MUTEX_RELEASE(&cachelock);

	return e;
}

void arp_init() {
	ringbuffer_init(&processbuffer, sizeof(bufferentry_t) * 1000);

	entryallocator = slab_newcache(sizeof(entry_t), 0, NULL, NULL);
	__assert(entryallocator);

	handlerthread = sched_newthread(handlerthreadfn, PAGE_SIZE * 4, 0, NULL, NULL);
	__assert(handlerthread);
	cleanupthread = sched_newthread(cleanupthreadfn, PAGE_SIZE * 4, 0, NULL, NULL);
	__assert(cleanupthread);

	MUTEX_INIT(&cachelock);

	SEMAPHORE_INIT(&psem, 0);

	SPINLOCK_INIT(ringlock);

	__assert(hashtable_init(&caches, 10) == 0);

	sched_queue(handlerthread);
	sched_queue(cleanupthread);
}
