#include <kernel/init.h>
#include <kernel/pci.h>
#include <kernel/alloc.h>
#include <kernel/pmm.h>
#include <kernel/block.h>
#include <logging.h>

typedef struct {
	uint32_t cap; // host capabilities
	uint32_t ghc; // global host control
	uint32_t is; // interrupt status
	uint32_t pi; // ports implemented
	uint32_t version; // version
	uint32_t ccc_ctl; // command completion coalescing control
	uint32_t ccc_ports; // command completion coalescing ports
	uint32_t em_loc; // enclosure management location
	uint32_t em_ctl; // encolosure management control
	uint32_t ext_capabilities; // host extended capabilities
	uint32_t bohc; // BIOS/OS handoff control and status
} __attribute__((packed)) ghc_t;

#define CAP_NCS(x) ((((x) >> 8) & 0x1f) + 1)
#define CAP_64BIT (1 << 31)

#define GHC_IE 2
#define GHC_AE (1 << 31)

#define EXTCAP_BOH 1

#define BOHC_BOS 1
#define BOHC_OOS 2
#define BOHC_OOC 8
#define BOHC_BB 16

typedef struct {
	uint32_t clb; // command list base address
	uint32_t clbu; // command list base address upper 32 bits
	uint32_t fb; // FIS base
	uint32_t fbu; // FIS base upper 32 bits
	uint32_t is; // interrupt status
	uint32_t ie; // interrupt enable
	uint32_t cmd; // command and status
	uint32_t reserved;
	uint32_t tfd; // task file data
	uint32_t sig; // signature
	uint32_t ssts; // serial ata status
	uint32_t sctl; // serial ata control
	uint32_t serr; // serial ata error
	uint32_t sact; // serial ata active
	uint32_t ci; // command issue
	uint32_t snft; // serial ata notification
	uint32_t fbs; // fis-based switching control
	uint32_t devslp; // device sleep
	uint32_t reserved2[10];
	uint32_t vs[4]; // vendor specific
} __attribute__((packed)) port_t;

#define CMD_ST 1
#define CMD_FRE (1 << 4)
#define CMD_FR (1 << 14)
#define CMD_CR (1 << 15)

#define TFD_ERROR 1
#define TFD_DRQ (1 << 3)
#define TFD_BSY (1 << 7)

#define SSTS_DET(x) ((x) & 0xf)
#define SSTS_DET_OK 3

#define SIG_ATA 0x101

#define IE_D2H 1
#define IE_PSS 2
#define IE_IFE (1 << 27)
#define IE_HBDE (1 << 28)
#define IE_HBFE (1 << 29)
#define IE_TFE (1 << 30)

#define FIS_TYPE_H2D 0x27

typedef struct {
	uint8_t type;
	uint8_t flags;
	uint8_t command;
	uint8_t features;
	uint8_t lba_low;
	uint8_t lba_mid;
	uint8_t lba_high;
	uint8_t device;
	uint8_t lba_low_exp;
	uint8_t lba_mid_exp;
	uint8_t lba_high_exp;
	uint8_t features_exp;
	uint8_t sector_count;
	uint8_t sector_count_exp;
	uint8_t reserved;
	uint8_t control;
	uint32_t reserved2;
} __attribute__((packed)) fis_h2d_t;

#define FIS_H2D_FLAGS_COMMAND (1 << 7)
#define FIS_H2D_DEVICE_LBA (1 << 6)
#define ATA_COMMAND_READ_DMA_EXT 0x25
#define ATA_COMMAND_WRITE_DMA_EXT 0x35
#define ATA_COMMAND_IDENTIFY 0xec

typedef struct {
	uint32_t base_low;
	uint32_t base_high;
	uint32_t reserved;
	uint32_t byte_count_and_flags;
} prdt_t;

typedef struct {
	fis_h2d_t fis;
	uint8_t padding[64 - sizeof(fis_h2d_t)];
	uint8_t atapi_cmd[16];
	uint8_t reserved[48];
	prdt_t prdt[];
} __attribute__((packed)) command_table_t;

typedef struct {
	uint16_t flags;
	uint16_t prdtl;
	uint32_t prdbc;
	uint32_t table_base_low;
	uint32_t table_base_high;
	uint32_t reserved[4];
} __attribute__((packed)) command_header_t;

#define CMDHDR_WRITE (1 << 6)

typedef struct {
	uint16_t ignore0[100];
	uint16_t lba48_size[4];
	uint16_t ignore1[152];
} identify_t;

struct ahci_t;
typedef struct {
	struct ahci_t *ahci;
	command_header_t *command_list;
	spinlock_t command_lock;
	semaphore_t command_semaphore;
	dpc_t dpc;
	int command_waiting;
	int command_next;
	size_t sector_count;
	thread_t *waiters[32];
} port_data_t;

typedef struct ahci_t {
	volatile ghc_t *ghc;
	volatile port_t *ports;
	uint32_t implemented_ports;
	size_t command_slot_count;
	int id;
	port_data_t *port_data[32];
} ahci_t;

#define FOR_EACH_PORT(ahci) \
	for (int i = 0, _pi = (ahci)->implemented_ports; _pi; i++, _pi >>= 1) \
		if (_pi & 1)

void ahci_dpc(context_t *, dpcarg_t arg) {
	port_data_t *port_data = arg;
	int port;
	for (port = 0; port_data->ahci->port_data[port] != port_data; ++port);
	int cmd_slots = port_data->ahci->command_slot_count;

	spinlock_acquire(&port_data->command_lock);

	bool error_happened = (port_data->ahci->ports[port].tfd & TFD_ERROR) || port_data->ahci->ports[port].serr;
	int error_slot = -1;
	if (error_happened) {
		// an error happened, restart the port and notify last waiter
		uint32_t ci = port_data->ahci->ports[port].ci;
		port_data->ahci->ports[port].cmd &= ~(CMD_ST | CMD_FRE);
		while (port_data->ahci->ports[port].cmd & (CMD_CR | CMD_FR)) CPU_PAUSE();
		port_data->ahci->ports[port].serr = port_data->ahci->ports[port].serr;
		__assert((port_data->ahci->ports[port].tfd & (TFD_BSY | TFD_DRQ)) == 0); // handling this is a TODO

		// find errored out slot
		for (
		    error_slot = port_data->command_waiting;
		    (ci & (1 << error_slot)) == 0;
		    error_slot = (error_slot + 1) % cmd_slots
		    );

		// clear ci bit
		ci &= ~(1 << error_slot);

		// enable command processing again
		port_data->ahci->ports[port].cmd |= CMD_FRE;
		while ((port_data->ahci->ports[port].cmd & CMD_FR) == 0) CPU_PAUSE();
		port_data->ahci->ports[port].cmd |= CMD_ST;
		while ((port_data->ahci->ports[port].cmd & CMD_CR) == 0) CPU_PAUSE();

		// resend commands
		for (int slot = port_data->command_waiting; port_data->waiters[slot]; slot = (slot + 1) % cmd_slots) {
			if (ci & (1 << slot))
				port_data->ahci->ports[port].ci |= (1 << slot);
		}
	}

	// notify waiters about command completion
	while (port_data->waiters[port_data->command_waiting] && 
	(port_data->ahci->ports[port].ci & (1 << port_data->command_waiting)) == 0) {
		thread_t *thread = port_data->waiters[port_data->command_waiting];
		port_data->waiters[port_data->command_waiting] = NULL;

		sched_wakeup(thread, port_data->command_waiting == error_slot ? EIO : SCHED_WAKEUP_REASON_NORMAL);

		port_data->command_waiting = (port_data->command_waiting + 1) % cmd_slots;

		semaphore_signal(&port_data->command_semaphore);
	}

	spinlock_release(&port_data->command_lock);
}

void ahci_isr(isr_t *isr, context_t *) {
	ahci_t *ahci = isr->priv;

	uint32_t ports_pending = ahci->ghc->is;
	if (!ports_pending)
		return;

	for (int i = __builtin_ctz(ports_pending); ports_pending; i = __builtin_ctz(ports_pending)) {
		if (ahci->port_data[i] == NULL)
			continue;

		dpc_enqueue(&ahci->port_data[i]->dpc, ahci_dpc, ahci->port_data[i]);

		ports_pending &= ~(1 << i);
		ahci->ports[i].is = ahci->ports[i].is;
		ahci->ghc->is = 1 << i;
	}
}

int dispatch_command_and_wait(ahci_t *ahci, int port, command_header_t *command_header) {
	__assert(ahci->ports[port].cmd & CMD_ST);
	semaphore_wait(&ahci->port_data[port]->command_semaphore, false);

	long ipl = spinlock_acquire_raise_ipl(&ahci->port_data[port]->command_lock, IPL_DPC);

	// prepare slot
	memcpy(&ahci->port_data[port]->command_list[ahci->port_data[port]->command_next], command_header, sizeof(command_header_t));
	ahci->port_data[port]->waiters[ahci->port_data[port]->command_next] = current_thread();

	// dispatch to device
	ahci->ports[port].ci = 1 << ahci->port_data[port]->command_next;

	// wait for command to complete
	ahci->port_data[port]->command_next = (ahci->port_data[port]->command_next + 1) % ahci->command_slot_count;
	sched_prepare_sleep(false);
	spinlock_release_lower_ipl(&ahci->port_data[port]->command_lock, ipl);
	return sched_yield();
}

static int cmd_identify(ahci_t *ahci, int port, identify_t *results_phys) {
	// TODO handle pmp
	command_header_t command_header = {
		.flags = sizeof(fis_h2d_t) / 4,
		.prdtl = 1,
	};

	command_table_t *command_table_phys = pmm_allocpage(PMM_SECTION_DEFAULT);
	if (command_table_phys == NULL)
		return ENOMEM;

	command_header.table_base_low = (uintptr_t)command_table_phys & 0xffffffff;
	command_header.table_base_high = ((uintptr_t)command_table_phys >> 32) & 0xffffffff;

	command_table_t *command_table = MAKE_HHDM(command_table_phys);
	memset(command_table, 0, sizeof(command_table_t));
	command_table->fis.type = FIS_TYPE_H2D;
	command_table->fis.command = ATA_COMMAND_IDENTIFY;
	command_table->fis.flags = FIS_H2D_FLAGS_COMMAND;
	command_table->prdt[0].base_low = (uintptr_t)results_phys & 0xffffffff;
	command_table->prdt[0].base_high = ((uintptr_t)results_phys >> 32) & 0xffffffff;
	command_table->prdt[0].byte_count_and_flags = 511;

	int error = dispatch_command_and_wait(ahci, port, &command_header);

	pmm_release(command_table_phys);

	return error;
}

#define PRDTL_LIMIT ((PAGE_SIZE - sizeof(command_table_t)) / sizeof(prdt_t))
static int setup_prdt(iovec_iterator_t *iterator, command_table_t *command_table, uint16_t *prdtl, size_t *requested_size) {
	int err;
	size_t block_done = 0;
	size_t prdt_done = 0;
	while (block_done < *requested_size && prdt_done < PRDTL_LIMIT) {
		void *page;
		size_t page_offset, page_remaining;
		err = iovec_iterator_next_page(iterator, &page_offset, &page_remaining, &page);
		if (err)
			return err;

		__assert(page);
		// not block aligned
		if (page_remaining % 512) {
			pmm_release(page);
			return EINVAL;
		}

		size_t blocks_in_page = min(page_remaining / 512, *requested_size - block_done);
		command_table->prdt[prdt_done].base_low = ((uintptr_t)page + page_offset) & 0xffffffff;
		command_table->prdt[prdt_done].base_high = (((uintptr_t)page + page_offset) >> 32) & 0xffffffff;
		command_table->prdt[prdt_done].byte_count_and_flags = blocks_in_page * 512 - 1;

		// if we didnt use the whole space in the page, set the iterator back a bit
		size_t diff_between_available_and_used = page_remaining - blocks_in_page * 512;
		if (diff_between_available_and_used) {
			size_t iterator_offset = iovec_iterator_total_offset(iterator);
			iovec_iterator_set(iterator, iterator_offset - diff_between_available_and_used);
		}

		block_done += blocks_in_page;
		++prdt_done;
	}

	*requested_size = block_done;
	*prdtl = prdt_done;
	return 0;
}

static void release_prdt(command_table_t *command_table, uint16_t prdtl) {
	for (int i = 0; i < prdtl; ++i) {
		void *address = (void *)((uint64_t)command_table->prdt[i].base_low | ((uint64_t)command_table->prdt[i].base_high << 32));
		pmm_release(address);
	}
}

static int rw(port_data_t *port_data, iovec_iterator_t *iterator, uintmax_t lba, size_t count, bool write) {
	// TODO handle pmp
	int port;
	for (port = 0; port_data->ahci->port_data[port] != port_data; ++port);
	command_header_t command_header = {
		.flags = (sizeof(fis_h2d_t) / 4) | (write ? CMDHDR_WRITE : 0),
	};

	command_table_t *command_table_phys = pmm_allocpage(PMM_SECTION_DEFAULT);
	if (command_table_phys == NULL)
		return ENOMEM;

	command_header.table_base_low = (uintptr_t)command_table_phys & 0xffffffff;
	command_header.table_base_high = ((uintptr_t)command_table_phys >> 32) & 0xffffffff;

	command_table_t *command_table = MAKE_HHDM(command_table_phys);
	memset(command_table, 0, sizeof(command_table_t));
	command_table->fis.type = FIS_TYPE_H2D;
	command_table->fis.command = write ? ATA_COMMAND_WRITE_DMA_EXT : ATA_COMMAND_READ_DMA_EXT;
	command_table->fis.flags = FIS_H2D_FLAGS_COMMAND;
	command_table->fis.device = FIS_H2D_DEVICE_LBA;

	size_t done = 0;
	int error = 0;
	while (done != count) {
		size_t do_count = count - done;
		uint16_t prdtl_tmp;
		error = setup_prdt(iterator, command_table, &prdtl_tmp, &do_count);
		if (error)
			break;

		command_header.prdtl = prdtl_tmp;

		uintmax_t this_lba = lba + done;
		command_table->fis.lba_low = this_lba & 0xff;
		command_table->fis.lba_mid = (this_lba >> 8) & 0xff;
		command_table->fis.lba_high = (this_lba >> 16) & 0xff;
		command_table->fis.lba_low_exp = (this_lba >> 24) & 0xff;
		command_table->fis.lba_mid_exp = (this_lba >> 32) & 0xff;
		command_table->fis.lba_high_exp = (this_lba >> 40) & 0xff;
		command_table->fis.sector_count = do_count & 0xff;
		command_table->fis.sector_count_exp = (do_count >> 8) & 0xff;

		error = dispatch_command_and_wait(port_data->ahci, port, &command_header);

		release_prdt(command_table, prdtl_tmp);

		if (error)
			break;

		done += do_count;
	}

	pmm_release(command_table_phys);

	return error;
}

static int write(void *private, iovec_iterator_t *buffer, uintmax_t lba, size_t count) {
	return rw(private, buffer, lba, count, true);
}

static int read(void *private, iovec_iterator_t *buffer, uintmax_t lba, size_t count) {
	return rw(private, buffer, lba, count, false);
}

static void init_port(ahci_t *ahci, int port) {
	// send identify command
	identify_t *identify_phys = pmm_allocpage(PMM_SECTION_DEFAULT);
	__assert(identify_phys);
	identify_t *identify = MAKE_HHDM(identify_phys);
	memset(identify, 0, 512);

	if (cmd_identify(ahci, port, identify_phys)) {
		pmm_release(identify_phys);
		return;
	}

	ahci->port_data[port]->sector_count = (uint64_t)identify->lba48_size[0] | ((uint64_t)identify->lba48_size[1] << 16) | 
				((uint64_t)identify->lba48_size[2] << 32) | ((uint64_t)identify->lba48_size[3] << 48);

	printf("ahci%dp%d: ATA drive with %lu sectors\n", ahci->id, port, ahci->port_data[port]->sector_count);

	char name[10];
	snprintf(name, 10, "ahci%lup%lu", ahci->id, port);

	blockdesc_t desc = {
		.private = ahci->port_data[port],
		.type = BLOCK_TYPE_DISK,
		.blockcapacity = ahci->port_data[port]->sector_count,
		.blocksize = 512,
		.read = read,
		.write = write
	};

	block_register(&desc, name);
}

static void init_controller(pcienum_t *pci_enum) {
	static int controller_id;
	pci_setcommand(pci_enum, PCI_COMMAND_MMIO, 1);
	pci_setcommand(pci_enum, PCI_COMMAND_IO, 0);
	pci_setcommand(pci_enum, PCI_COMMAND_BUSMASTER, 1);
	pci_setcommand(pci_enum, PCI_COMMAND_IRQDISABLE, 1);

	printf("ahci: found at %02x:%02x.%x\n", pci_enum->bus, pci_enum->device, pci_enum->function);

	pcibar_t pci_bar = pci_getbar(pci_enum, 5);
	__assert(pci_bar.mmio);

	if (pci_enum->msi.exists) {
		pci_initmsi(pci_enum, 1);
	} else {
		printf("ahci: no support for msi\n");
		return;
	}

	int id = controller_id++;

	ahci_t *ahci = alloc(sizeof(ahci_t));
	__assert(ahci);
	ahci->ghc = (ghc_t *)pci_bar.address;
	ahci->ports = (volatile port_t *)((uintptr_t)pci_bar.address + 0x100);

	// enable ahci mode
	ahci->ghc->ghc |= GHC_AE;

	// check for 64-bit hba
	// XXX while this shouldn't be a hard requirement, it sure does make life a LOT easier
	// not having to deal with 32-bit DMA.
	if (!(ahci->ghc->cap & CAP_64BIT)) {
		printf("ahci%d: no support for 64-bit addressing, aborting\n", id);
		free(ahci);
		return;
	}

	// take ownership from bios, if applicable
	if ((ahci->ghc->ext_capabilities & EXTCAP_BOH) == 0)
		goto handoff_done; // no need to do handoff

	ahci->ghc->bohc |= BOHC_OOS;

	if (ahci->ghc->bohc & BOHC_BOS) {
		// BIOS has ownership. the spec says that if the bios busy bit
		// is set within 25 ms, we have to do a longer wait. otherwise, the hba is ours
		sched_sleep_us(25000);
		if ((ahci->ghc->bohc & BOHC_BB) == 0 && (ahci->ghc->bohc & BOHC_BOS) == 0)
			goto handoff_done;

		printf("ahci%d: BIOS busy, waiting\n", id);
		// BIOS is busy, wait the two seconds the spec mandates
		sched_sleep_us(2000000);
		if ((ahci->ghc->bohc & BOHC_BOS) == 0)
			goto handoff_done;

		// still busy, sleep in increments of 1 second 8 times
		for (int i = 0; i < 8; ++i) {
			sched_sleep_us(1000000);

			if ((ahci->ghc->bohc & BOHC_BOS) == 0)
				goto handoff_done;
		}

		// timeout, bail
		printf("ahci%d: BIOS handoff took too long, bailing\n", id);
		free(ahci);
		return;
	}

	handoff_done:

	// gather some information about the controller
	ahci->command_slot_count = CAP_NCS(ahci->ghc->cap);
	ahci->implemented_ports = ahci->ghc->pi;
	ahci->id = id;

	// disable interrupts globally
	ahci->ghc->ghc &= ~GHC_IE;

	// initialize ports
	void *command_slot_mem = pmm_allocpage(PMM_SECTION_DEFAULT);
	__assert(command_slot_mem);
	size_t command_slot_offset = 0;

	void *fis_base_mem = pmm_allocpage(PMM_SECTION_DEFAULT);
	__assert(fis_base_mem);
	size_t fis_base_offset = 0;
	FOR_EACH_PORT(ahci) {
		// ensure that they are idle
		if (ahci->ports[i].cmd & (CMD_ST | CMD_CR | CMD_FRE | CMD_FR)) {
			// port is not idle, idle it
			ahci->ports[i].cmd &= ~(CMD_ST | CMD_FRE);
			int j;

			// spec says to wait at least 500 milliseconds
			for (j = 0; j < 50; ++j) {
				sched_sleep_us(10000);
				if ((ahci->ports[i].cmd & (CMD_FR | CMD_CR)) == 0)
					break;
			}

			if (j == 50) {
				printf("ahci%d: port %d did not go idle. Port reset is a TODO.\n", id, i);
				ahci->implemented_ports &= ~(1 << i);
				continue;
			}
		}

		// check if somethin is actually connected and if it is something we care about
		if (SSTS_DET(ahci->ports[i].ssts) != SSTS_DET_OK || ahci->ports[i].sig != SIG_ATA) {
			ahci->implemented_ports &= ~(1 << i);
			continue;
		}


		// program command list base
		void *cmd_ptr = (void *)((uintptr_t)command_slot_mem + command_slot_offset);
		memset(MAKE_HHDM(cmd_ptr), 0, 1024);

		ahci->ports[i].clb = (uintptr_t)cmd_ptr & 0xffffffff;
		ahci->ports[i].clbu = ((uintptr_t)cmd_ptr >> 32) & 0xffffffff;

		command_slot_offset += 1024;
		if (command_slot_offset >= PAGE_SIZE) {
			// used up the page, allocate another
			command_slot_mem = pmm_allocpage(PMM_SECTION_DEFAULT);
			__assert(command_slot_mem);
			command_slot_offset = 0;
		}

		// initialize FIS base
		void *fis_ptr = (void *)((uintptr_t)fis_base_mem + fis_base_offset);
		memset(MAKE_HHDM(fis_ptr), 0, 256);

		ahci->ports[i].fb = (uintptr_t)fis_ptr & 0xffffffff;
		ahci->ports[i].fbu = ((uintptr_t)fis_ptr >> 32) & 0xffffffff;

		fis_base_offset += 256;
		if (fis_base_offset >= PAGE_SIZE) {
			// used up the page, allocate another
			fis_base_mem = pmm_allocpage(PMM_SECTION_DEFAULT);
			__assert(fis_base_mem);
			fis_base_offset = 0;
		}

		// clear serr
		ahci->ports[i].serr = ahci->ports[i].serr;

		// mask all irqs and clear irq status
		ahci->ports[i].ie = 0;
		ahci->ports[i].is = 0xffffffff;

		// enable FIS receive
		ahci->ports[i].cmd |= CMD_FRE;

		// wait for it to start
		// TODO timeout?
		while (!(ahci->ports[i].cmd & CMD_FR)) sched_sleep_us(100);

		// start command processing
		ahci->ports[i].cmd |= CMD_ST;

		// wait for it to start
		// TODO timeout?
		while (!(ahci->ports[i].cmd & CMD_CR)) sched_sleep_us(100);

		// initialize port data structure
		port_data_t *port_data = alloc(sizeof(port_data_t));
		__assert(port_data);

		port_data->ahci = ahci;
		port_data->command_list = MAKE_HHDM(cmd_ptr);
		SPINLOCK_INIT(port_data->command_lock);
		SEMAPHORE_INIT(&port_data->command_semaphore, ahci->command_slot_count);

		ahci->port_data[i] = port_data;
	}

	if (command_slot_offset == 0)
		pmm_release(command_slot_mem);
	if (fis_base_offset == 0)
		pmm_release(fis_base_mem);

	// clear global interrupt status
	ahci->ghc->is = 0xffffffff;

	// configure interrupts for each port
	FOR_EACH_PORT(ahci) {
		ahci->ports[i].ie = IE_D2H | IE_IFE | IE_HBDE | IE_HBFE | IE_TFE | IE_PSS;
	}

	// set up MSI
	isr_t *isr = interrupt_allocate(ahci_isr, ARCH_EOI, IPL_DISK);
	__assert(isr);
	isr->priv = ahci;
	pci_msisetbase(pci_enum, INTERRUPT_IDTOVECTOR(isr->id), 1, 0);

	// enable interrupts globally
	ahci->ghc->ghc |= GHC_IE;

	// the controller has been initialized, now its time to figure out who the devices are
	FOR_EACH_PORT(ahci) {
		init_port(ahci, i);
	}
}

static void ahci_init() {
	int i = 0;
	for (;;) {
		pcienum_t *e = pci_getenum(PCI_CLASS_STORAGE, PCI_SUBCLASS_STORAGE_SATA, PCI_PROGIF_STORAGE_SATA_AHCI, -1, -1, -1, i++);
		if (e == NULL)
			break;
		init_controller(e);
	}
}

INIT_ROUTINE_DEFINE(ahci, INIT_ROUTINE_FLAGS_NONE, ahci_init, acpi);
