#include <kernel/init.h>
#include <kernel/pci.h>
#include <logging.h>
#include <arch/io.h>
#include <kernel/net.h>
#include <kernel/scheduler.h>
#include <kernel/pmm.h>
#include <kernel/eth.h>
#include <kernel/interrupt.h>

#define REGISTER_MAC0 0x0
#define REGISTER_MAC4 0x4
#define REGISTER_RX_RING_LOW 0x20
#define REGISTER_RX_RING_HIGH 0x24
#define REGISTER_COMMAND 0x37
#define 	REGISTER_COMMAND_RX_ENABLE 4
#define 	REGISTER_COMMAND_TX_ENABLE 8
#define 	REGISTER_COMMAND_RESET 16
#define REGISTER_TRANSMIT_PRIORITY_POLLING 0x38
#define 	REGISTER_TRANSMIT_PRIORITY_POLLING_NORMAL (1 << 6)
#define REGISTER_IRQ_MASK 0x3c
#define 	REGISTER_IRQ_MASK_RX_OK 1
#define 	REGISTER_IRQ_MASK_RX_ERROR 2
#define 	REGISTER_IRQ_MASK_TX_OK 4
#define 	REGISTER_IRQ_MASK_TX_ERROR 8
#define REGISTER_IRQ_STATUS 0x3e
#define 	REGISTER_IRQ_STATUS_RX_OK 1
#define 	REGISTER_IRQ_STATUS_RX_ERROR 2
#define 	REGISTER_IRQ_STATUS_TX_OK 4
#define 	REGISTER_IRQ_STATUS_TX_ERROR 8
#define REGISTER_TX_CONFIG 0x40
#define 	REGISTER_TX_CONFIG_DMA_BURST_UNLIMITED (7 << 8)
#define 	REGISTER_TX_CONFIG_IFG_NORMAL (3 << 24)
#define REGISTER_RX_CONFIG 0x44
#define 	REGISTER_RX_CONFIG_ACCEPT_ALL_WITHIN_COMMON_SENSE 0xe
#define 	REGISTER_RX_CONFIG_DMA_BURST_UNLIMITED (7 << 8)
#define 	REGISTER_RX_CONFIG_FIFO_THRESHOLD_NONE (7 << 13)
#define REGISTER_PHYAR 0x60
#define 	REGISTER_PHYAR_WRITE (1 << 31)
#define REGISTER_RX_MAX_SIZE 0xda
#define REGISTER_CCR 0xe0
#define REGISTER_TX_RING_LOW 0xe4
#define REGISTER_TX_RING_HIGH 0xe8
#define REGISTER_TX_MAX_SIZE 0xec

#define PHY_BMCR 0
#define		PHY_BMCR_RESTART_AUTO (1 << 9)
#define 	PHY_BMCR_AUTO (1 << 12)
#define 	PHY_BMCR_RESET (1 << 15)
#define PHY_BMSR 1
#define 	PHY_BMSR_LINK_STATUS (1 << 2)
#define		PHY_BMSR_AN_COMPLETE (1 << 5)

#define RX_DESCRIPTOR_COUNT 1024
#define TX_DESCRIPTOR_COUNT 1024
#define RX_BUFFER_SIZE 1524

#define DESCRIPTOR_OWN (1 << 15)
#define DESCRIPTOR_EOR (1 << 14)

typedef struct {
	uint16_t length;
	uint16_t flags;
	uint32_t vlan;
	uint32_t addr_low;
	uint32_t addr_high;
} __attribute__((packed)) descriptor_t;

typedef struct {
	netdev_t netdev;
	descriptor_t *tx_ring;
	descriptor_t *rx_ring;
	dpc_t tx_dpc;
	dpc_t rx_dpc;
	pcibar_t bar;
	int tx_wait;
	int tx_last;
	semaphore_t tx_semaphore;
	int rx_next;
	spinlock_t tx_lock;
	thread_t *tx_waiters[TX_DESCRIPTOR_COUNT];
} rtl8169dev_t;

static bool phy_read(pcibar_t bar, uint8_t reg, uint16_t *value) {
        outd(bar.address + REGISTER_PHYAR, reg << 16);
        for (int i = 0; i < 30; ++i) {
                if (ind(bar.address + REGISTER_PHYAR) & REGISTER_PHYAR_WRITE) {
                        *value = ind(bar.address + REGISTER_PHYAR) & 0xffff;
                        return true;
                }

                sched_sleep_us(100);
        }

        return false;
}

static bool phy_write(pcibar_t bar, uint8_t reg, uint16_t value) {
        outd(bar.address + REGISTER_PHYAR, (uint32_t)value | ((uint32_t)reg << 16) | REGISTER_PHYAR_WRITE);
        for (int i = 0; i < 30; ++i) {
                if (!(ind(bar.address + REGISTER_PHYAR) & REGISTER_PHYAR_WRITE))
                        return true;

                sched_sleep_us(100);
        }
        return false;
}

static void rtl8169_dpc_tx(context_t *, dpcarg_t arg) {
	rtl8169dev_t *dev = arg;

	spinlock_acquire(&dev->tx_lock);

	do {
		if (dev->tx_ring[dev->tx_wait].flags & DESCRIPTOR_OWN)
			break;

		thread_t *thread = dev->tx_waiters[dev->tx_wait];
		dev->tx_waiters[dev->tx_wait] = NULL;

		dev->tx_wait = (dev->tx_wait + 1) % TX_DESCRIPTOR_COUNT;

		semaphore_signal(&dev->tx_semaphore);
		sched_wakeup(thread, SCHED_WAKEUP_REASON_NORMAL);
	} while (dev->tx_wait != dev->tx_last);

	spinlock_release(&dev->tx_lock);
}

static void rtl8169_dpc_rx(context_t *, dpcarg_t arg) {
	rtl8169dev_t *dev = arg;

	while (!(dev->rx_ring[dev->rx_next].flags & DESCRIPTOR_OWN)) {
		descriptor_t *descriptor = &dev->rx_ring[dev->rx_next];
		void *buffer = MAKE_HHDM((void *)(descriptor->addr_low | ((uint64_t)descriptor->addr_high << 32)));

		eth_process(&dev->netdev, buffer);

		uint16_t eor = descriptor->flags & DESCRIPTOR_EOR;
		descriptor->length = RX_BUFFER_SIZE;
		descriptor->flags = eor | DESCRIPTOR_OWN;

		dev->rx_next = (dev->rx_next + 1) % RX_DESCRIPTOR_COUNT;
	}
}

static void rtl8169_isr(isr_t *self, context_t *) {
	rtl8169dev_t *dev = self->priv;

	uint16_t status = inw(dev->bar.address + REGISTER_IRQ_STATUS);
	if (!status)
		return;

	uint16_t clear = 0;

	if (status & (REGISTER_IRQ_STATUS_TX_OK | REGISTER_IRQ_STATUS_TX_ERROR)) {
		dpc_enqueue(&dev->tx_dpc, rtl8169_dpc_tx, dev);
		clear |= REGISTER_IRQ_STATUS_TX_OK | REGISTER_IRQ_STATUS_TX_ERROR;
	}

	if (status & (REGISTER_IRQ_STATUS_RX_OK | REGISTER_IRQ_STATUS_RX_ERROR)) {
		dpc_enqueue(&dev->rx_dpc, rtl8169_dpc_rx, dev);
		clear |= REGISTER_IRQ_STATUS_RX_OK | REGISTER_IRQ_STATUS_RX_ERROR;
	}

	outw(dev->bar.address + REGISTER_IRQ_STATUS, clear);
}

static int rtl8169_sendpacket(netdev_t *internal, netdesc_t desc, mac_t target, int proto) {
	rtl8169dev_t *netdev = (rtl8169dev_t *)internal;

	ethframe_t *ethframe = desc.address;
	ethframe->type = cpu_to_be_w(proto);
	memcpy(&ethframe->source, &netdev->netdev.mac, sizeof(mac_t));
	memcpy(&ethframe->destination, &target, sizeof(mac_t));

	semaphore_wait(&netdev->tx_semaphore, false);

	long ipl = spinlock_acquire_raise_ipl(&netdev->tx_lock, IPL_NET);

	netdev->tx_last = (netdev->tx_last + 1) % TX_DESCRIPTOR_COUNT;
	descriptor_t *descriptor = &netdev->tx_ring[netdev->tx_last];

	uintptr_t physical_address = (uintptr_t)FROM_HHDM(desc.address);
	descriptor->addr_low = physical_address & 0xffffffff;
	descriptor->addr_high = (physical_address >> 32) & 0xffffffff;
	descriptor->length = desc.size;
	descriptor->flags |= DESCRIPTOR_OWN;

	netdev->tx_waiters[netdev->tx_last] = current_thread();

	outb(netdev->bar.address + REGISTER_TRANSMIT_PRIORITY_POLLING, REGISTER_TRANSMIT_PRIORITY_POLLING_NORMAL);

	sched_prepare_sleep(false);
	spinlock_release_lower_ipl(&netdev->tx_lock, ipl);
	sched_yield();

	return 0;
}

// requested size doesn't account for ethernet header
static int rtl8169_allocdesc(netdev_t *netdev, size_t requested_size, netdesc_t *desc) {
	__assert(requested_size <= netdev->mtu);
	size_t true_size = sizeof(ethframe_t) + requested_size;
	void *physical = pmm_allocpage(PMM_SECTION_DEFAULT);
	if (physical == NULL)
		return ENOMEM;

	desc->address = MAKE_HHDM(physical);
	desc->size = true_size;
	desc->curroffset = sizeof(ethframe_t);
	return 0;
}

static int rtl8169_freedesc(netdev_t *netdev, netdesc_t *desc) {
	pmm_release(FROM_HHDM(desc->address));
	return 0;
}

static int controller_id = 0;

static void init_controller(pcienum_t *pci_enum) {
	pci_setcommand(pci_enum, PCI_COMMAND_MMIO, 0);
	pci_setcommand(pci_enum, PCI_COMMAND_IO, 1);
	pci_setcommand(pci_enum, PCI_COMMAND_BUSMASTER, 1);
	pci_setcommand(pci_enum, PCI_COMMAND_IRQDISABLE, 1);

	printf("rtl8169: found at %02x:%02x.%x\n", pci_enum->bus, pci_enum->device, pci_enum->function);

	int id = controller_id++;

	pcibar_t pci_bar = pci_getbar(pci_enum, 0);
	__assert(!pci_bar.mmio);

	size_t int_count;
	if (pci_enum->msix.exists) {
		int_count = pci_initmsix(pci_enum);
	} else if (pci_enum->msi.exists) {
		printf("rtl8169: driver only supports msi-x currently\n");
		return;
	} else {
		printf("rtl8169: no support for msi-x or msi\n");
		return;
	}

	__assert(int_count);

	// according to the spec, the order of initialization is:
	// 1. C+CR
	// 2. Command
	// 3. Everything else

	outw(pci_bar.address + REGISTER_CCR, 0);
	outb(pci_bar.address + REGISTER_COMMAND, REGISTER_COMMAND_RESET);
	bool reset_ok = false;
	for (int i = 0; i < 50; ++i) {
		if ((inb(pci_bar.address + REGISTER_COMMAND) & REGISTER_COMMAND_RESET) == 0) {
			reset_ok = true;
			break;
		}

		sched_sleep_us(100);
	}

	if (!reset_ok) {
		printf("rtl8169.%d: software reset timed out\n", id);
		return;
	}

	// mask all interrupts and clear any pending
	outw(pci_bar.address + REGISTER_IRQ_MASK, 0);
	outw(pci_bar.address + REGISTER_IRQ_STATUS, 0xffff);

	// read mac address
	mac_t mac;
	uint32_t word = ind(pci_bar.address + REGISTER_MAC0);
	mac.address[0] = word & 0xff;
	mac.address[1] = (word >> 8) & 0xff;
	mac.address[2] = (word >> 16) & 0xff;
	mac.address[3] = (word >> 24) & 0xff;
	word = ind(pci_bar.address + REGISTER_MAC4);
	mac.address[4] = word & 0xff;
	mac.address[5] = (word >> 8) & 0xff;

	printf("rtl8169.%d: %x:%x:%x:%x:%x:%x\n", id, mac.address[0], mac.address[1], mac.address[2], mac.address[3], mac.address[4], mac.address[5]);

	// set up descriptor rings
	descriptor_t *tx_ring_phys = pmm_alloc(ROUND_UP(sizeof(descriptor_t) * TX_DESCRIPTOR_COUNT, PAGE_SIZE) / PAGE_SIZE, PMM_SECTION_DEFAULT);
	__assert(tx_ring_phys);
	descriptor_t *rx_ring_phys = pmm_alloc(ROUND_UP(sizeof(descriptor_t) * RX_DESCRIPTOR_COUNT, PAGE_SIZE) / PAGE_SIZE, PMM_SECTION_DEFAULT);
	__assert(rx_ring_phys);

	descriptor_t *tx_ring = MAKE_HHDM(tx_ring_phys);
	descriptor_t *rx_ring = MAKE_HHDM(rx_ring_phys);

	memset(tx_ring, 0, sizeof(descriptor_t) * TX_DESCRIPTOR_COUNT);
	memset(rx_ring, 0, sizeof(descriptor_t) * RX_DESCRIPTOR_COUNT);

	// fill rx descriptors
	// XXX figure out a better way of doing this
	void *rx_buffer = pmm_alloc(ROUND_UP((RX_DESCRIPTOR_COUNT * RX_BUFFER_SIZE), PAGE_SIZE) / PAGE_SIZE, PMM_SECTION_DEFAULT);
	__assert(rx_buffer);

	for (int i = 0; i < RX_DESCRIPTOR_COUNT; ++i) {
		uintptr_t addr = (uintptr_t)rx_buffer + i * RX_BUFFER_SIZE;
		rx_ring[i].addr_low = addr & 0xffffffff;
		rx_ring[i].addr_high = (addr >> 32) & 0xffffffff;
		rx_ring[i].length = RX_BUFFER_SIZE;
		rx_ring[i].flags |= DESCRIPTOR_OWN;
	}

	tx_ring[TX_DESCRIPTOR_COUNT - 1].flags |= DESCRIPTOR_EOR;
	rx_ring[RX_DESCRIPTOR_COUNT - 1].flags |= DESCRIPTOR_EOR;

	outd(pci_bar.address + REGISTER_TX_RING_LOW, (uintptr_t)tx_ring_phys & 0xffffffff);
	outd(pci_bar.address + REGISTER_TX_RING_HIGH, ((uintptr_t)tx_ring_phys >> 32) & 0xffffffff);
	outd(pci_bar.address + REGISTER_RX_RING_LOW, (uintptr_t)rx_ring_phys & 0xffffffff);
	outd(pci_bar.address + REGISTER_RX_RING_HIGH, ((uintptr_t)rx_ring_phys >> 32) & 0xffffffff);

	outd(pci_bar.address + REGISTER_RX_CONFIG, REGISTER_RX_CONFIG_ACCEPT_ALL_WITHIN_COMMON_SENSE | REGISTER_RX_CONFIG_DMA_BURST_UNLIMITED | REGISTER_RX_CONFIG_FIFO_THRESHOLD_NONE);

	outb(pci_bar.address + REGISTER_COMMAND, REGISTER_COMMAND_TX_ENABLE);
	outd(pci_bar.address + REGISTER_TX_CONFIG, REGISTER_TX_CONFIG_DMA_BURST_UNLIMITED | REGISTER_TX_CONFIG_IFG_NORMAL);
 
	outw(pci_bar.address + REGISTER_RX_MAX_SIZE, 1518);
	// 128 * 0xc, enough for an ethernet MTU (while 0xb would be enough, the datasheet says that it must be larger than the max size)
	outb(pci_bar.address + REGISTER_TX_MAX_SIZE, 0xc);

	isr_t *isr = interrupt_allocate(rtl8169_isr, ARCH_EOI, IPL_NET);
	__assert(isr);
	pci_msixadd(pci_enum, 0, INTERRUPT_IDTOVECTOR(isr->id), 1, 0);
	pci_msixsetmask(pci_enum, 0);
	outw(pci_bar.address + REGISTER_IRQ_MASK, REGISTER_IRQ_MASK_RX_OK | REGISTER_IRQ_MASK_TX_OK | REGISTER_IRQ_MASK_TX_ERROR | REGISTER_IRQ_MASK_RX_ERROR);

	// register in netdev infrastructure
	rtl8169dev_t *netdev = alloc(sizeof(rtl8169dev_t));
	__assert(netdev);
	netdev->netdev.mtu = 1500;
	netdev->netdev.sendpacket = rtl8169_sendpacket;
	netdev->netdev.allocdesc = rtl8169_allocdesc;
	netdev->netdev.freedesc = rtl8169_freedesc;
	__assert(hashtable_init(&netdev->netdev.arpcache, 30) == 0);

	netdev->tx_ring = tx_ring;
	netdev->rx_ring = rx_ring;
	netdev->bar = pci_bar;
	netdev->tx_last = -1;
	SEMAPHORE_INIT(&netdev->tx_semaphore, TX_DESCRIPTOR_COUNT);
	SPINLOCK_INIT(netdev->tx_lock);

	isr->priv = netdev;

	char name[10];
	snprintf(name, 10, "rtl8169%d", id);
	__assert(netdev_register((netdev_t *)netdev, name) == 0);

	// reset PHY
	if (!phy_write(pci_bar, PHY_BMCR, PHY_BMCR_RESET)) {
		printf("rtl8169%d: timeout while reseting PHY\n", id);
		return;
	}

	// wait for reset to complete
	reset_ok = false;
	for (int i = 0; i < 30; ++i) {
		uint16_t bmcr;
		if (!phy_read(pci_bar, PHY_BMCR, &bmcr))
			break;

		if ((bmcr & PHY_BMCR_RESET) == 0) {
			reset_ok = true;
			break;
		}

		sched_sleep_us(100);
	}

	if (!reset_ok) {
		printf("rtl8169%d: PHY reset timed out\n", id);
		return;
	}

	// start autonegotiation
	if (!phy_write(pci_bar, PHY_BMCR, PHY_BMCR_AUTO | PHY_BMCR_RESTART_AUTO)) {
		printf("rtl8169%d: PHY AN start timed out\n", id);
		return;
	}

	// clear latch
	uint16_t bmsr;
	if (!phy_read(pci_bar, PHY_BMSR, &bmsr)) {
		printf("rtl8169%d: PHY AN timed out\n", id);
		return;
	}

	// wait for autonegotiation to complete
	bool an_ok = false;
	for (int i = 0; i < 50000; ++i) {
		if (!phy_read(pci_bar, PHY_BMSR, &bmsr))
			break;

		if ((bmsr & (PHY_BMSR_LINK_STATUS | PHY_BMSR_AN_COMPLETE)) == (PHY_BMSR_LINK_STATUS | PHY_BMSR_AN_COMPLETE)) {
			an_ok = true;
			break;
		}

		sched_sleep_us(100);
	}

	if (!an_ok) {
		printf("rtl8169%d: PHY AN timed out\n", id);
		return;
	}

	// enable rx/tx
	outb(pci_bar.address + REGISTER_COMMAND, REGISTER_COMMAND_TX_ENABLE | REGISTER_COMMAND_RX_ENABLE);
}

static int ids[] = {
	0x8168
};

void rtl8169_init() {
	for (int i = 0; i < sizeof(ids) / sizeof(ids[0]); ++i) {
		int off = 0;
		for (;;) {
			pcienum_t *e = pci_getenum(-1, -1, -1, 0x10ec, ids[i], -1, off++);
			if (e == NULL)
				break;
			init_controller(e);
		}
	}
}

INIT_ROUTINE_DEFINE(rtl8169, INIT_ROUTINE_FLAGS_NONE, rtl8169_init, acpi);
