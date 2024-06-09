#include <kernel/block.h>
#include <hashtable.h>
#include <mutex.h>
#include <logging.h>
#include <kernel/alloc.h>
#include <kernel/devfs.h>
#include <string.h>
#include <kernel/vmm.h>
#include <kernel/usercopy.h>

#define DISK_READ(desc, buf, lba, size) (desc)->read(desc->private, buf, lba, size)
#define DISK_WRITE(desc, buf, lba, size) (desc)->write(desc->private, buf, lba, size)

typedef struct {
	char signature[8];
	uint32_t revision;
	uint32_t size;
	uint32_t crc32;
	uint32_t reserved;
	uint64_t headerlba;
	uint64_t alternatelba;
	uint64_t firstusable;
	uint64_t lastusable;
	uint64_t guid[2];
	uint64_t entryarraylbastart;
	uint32_t entrycount;
	uint32_t entrybytesize;
	uint32_t entryarraycrc32;
} __attribute__((packed)) gptheader_t;

typedef struct {
	uint64_t typeguid[2];
	uint64_t guid[2];
	uint64_t startlba;
	uint64_t endlba;
	uint64_t attributes;
	// there should be a name here but it's unicode and we won't bother just yet
} __attribute__((packed)) gptentry_t;

typedef struct {
	uint8_t status;
	uint8_t chs[3];
	uint8_t type;
	uint8_t chsen[3];
	uint32_t lbastart;
	uint32_t lbasize;
} __attribute__((packed)) mbrentry_t;

#define MBR_BOOT_MAGIC 0xaa55
#define MBR_ENTRIES_OFFSET 0x1be
#define MBR_MAGIC_OFFSET 510
#define MBR_TYPE_FREE 0
#define MBR_TYPE_GPT 0xee

static mutex_t tablemutex;
static int currentid = 1;
static hashtable_t blocktable;

static blockdesc_t *getdesc(int id) {
	void *ret;
	MUTEX_ACQUIRE(&tablemutex, false);
	int err = hashtable_get(&blocktable, &ret, &id, sizeof(id));
	MUTEX_RELEASE(&tablemutex);
	return err ? NULL : ret;
}

static void bytestolba(blockdesc_t *desc, uintmax_t offset, size_t size, uintmax_t *lbaoffset, size_t *lbacount, uintmax_t *startoffset) {
	*lbaoffset = offset / desc->blocksize;
	*startoffset = offset % desc->blocksize;
	size_t end = offset + size;
	size_t toplba = ROUND_UP(end, desc->blocksize) / desc->blocksize;
	*lbacount = toplba - *lbaoffset;
}

#define MAP_FLAGS (ARCH_MMU_FLAGS_READ | ARCH_MMU_FLAGS_WRITE | ARCH_MMU_FLAGS_NOEXEC)

static int rwblock(int minor, void *buffer, size_t size, uintmax_t offset, int flags, bool write, size_t *done) {
	blockdesc_t *desc = getdesc(minor);
	if (desc == NULL)
		return ENODEV;

	uintmax_t bytetop = desc->blockcapacity * desc->blocksize;

	// offset past end
	if (offset >= bytetop) {
		*done = 0;
		return 0;
	}

	uintmax_t top = size + offset;
	// overflow check
	if (offset > top)
		top = -1l;

	// top of read goes past end
	if (top > bytetop)
		size = bytetop - offset;

	size_t lbaoffset, lbacount, startoffset;
	bytestolba(desc, offset, size, &lbaoffset, &lbacount, &startoffset);

	void *lbabuffer = vmm_map(NULL, lbacount * desc->blocksize, VMM_FLAGS_ALLOCATE, MAP_FLAGS, NULL);
	if (lbabuffer == NULL)
		return ENOMEM;

	int error = DISK_READ(desc, lbabuffer, lbaoffset + desc->lbaoffset, lbacount);
	if (error)
		goto cleanup;

	if (write) {
		memcpy((void *)((uintptr_t)lbabuffer + startoffset), buffer, size);
		error = DISK_WRITE(desc, lbabuffer, lbaoffset + desc->lbaoffset, lbacount);
		if (error)
			goto cleanup;
	}

	if (!write)
		memcpy(buffer, (void *)((uintptr_t)lbabuffer + startoffset), size);

	*done = size;

	cleanup:
	vmm_unmap(lbabuffer, lbacount * desc->blocksize, 0);
	return error;
}

static int read(int minor, void *buffer, size_t size, uintmax_t offset, int flags, size_t *readc) {
	return rwblock(minor, buffer, size, offset, flags, false, readc);
}

static int write(int minor, void *buffer, size_t size, uintmax_t offset, int flags, size_t *writec) {
	return rwblock(minor, buffer, size, offset, flags, true, writec);
}

static int ioctl(int minor, unsigned long request, void *arg, int *result) {
	blockdesc_t *desc = getdesc(minor);
	if (desc == NULL)
		return ENODEV;

	int ret = 0;

	switch (request) {
		case BLOCK_IOCTL_GETDESC:
			blockdesc_t copy = *desc;
			copy.write = NULL;
			copy.read = NULL;
			copy.private = NULL;
			ret = USERCOPY_POSSIBLY_TO_USER(arg, &copy, sizeof(blockdesc_t));
			break;
		default:
			ret = ENOTTY;
			break;
	}

	return ret;
}

static devops_t devops = {
	.read = read,
	.write = write,
	.ioctl = ioctl
};

static int registerdesc(blockdesc_t *desc, char *name) {
	MUTEX_ACQUIRE(&tablemutex, false);
	int id = currentid++;
	int ret = hashtable_set(&blocktable, desc, &id, sizeof(id), true);
	MUTEX_RELEASE(&tablemutex);

	if (ret)
		return ret;

	return devfs_register(&devops, name, V_TYPE_BLKDEV, DEV_MAJOR_BLOCK, id, 0644);
}

#define PART_NONE 0
#define PART_MBR 1
#define PART_GPT 2

static int detectpart(blockdesc_t *desc) {
	// read first two sectors
	void *sects = alloc(desc->blocksize * 2);
	__assert(sects);
	__assert(DISK_READ(desc, sects, 0, 2) == 0);

	void *lba0 = sects;
	void *lba1 = (void *)((uintptr_t)lba0 + desc->blocksize);
	mbrentry_t *mbrents = (mbrentry_t *)((uintptr_t)lba0 + MBR_ENTRIES_OFFSET);
	uint16_t bootmagic = *(uint16_t *)((uintptr_t)lba0 + MBR_MAGIC_OFFSET);
	int ret = PART_NONE;

	// TODO GPT checksum
	if (strncmp(lba1, "EFI PART", 8) == 0 && mbrents[0].type == MBR_TYPE_GPT && bootmagic == MBR_BOOT_MAGIC) 
		ret = PART_GPT;
	else if (bootmagic == MBR_BOOT_MAGIC)
		ret = PART_MBR;

	free(sects);
	return ret;
}

static void dogpt(blockdesc_t *desc, char *name) {
	// get header
	void *lba1 = alloc(desc->blocksize);
	__assert(lba1);
	__assert(DISK_READ(desc, lba1, 1, 1) == 0);
	gptheader_t *header = lba1;

	// get partition table
	size_t tablebytesize = header->entrybytesize * header->entrycount;
	size_t tablelbasize = ROUND_UP(tablebytesize, desc->blocksize) / desc->blocksize;
	void *tablebuffer = alloc(tablelbasize * desc->blocksize);
	__assert(tablebuffer);
	__assert(DISK_READ(desc, tablebuffer, header->entryarraylbastart, tablelbasize) == 0);

	// name + 'p' + 3 digits (max gpt partition count is 128) + '\0'
	size_t namebuflen = strlen(name) + 5;
	char partname[namebuflen];
	int partid = 1;

	for (int i = 0; i < header->entrycount; ++i) {
		gptentry_t *entry = (gptentry_t *)((uintptr_t)tablebuffer + header->entrybytesize * i);

		if (entry->typeguid[0] == 0 && entry->typeguid[1] == 0)
			continue;

		snprintf(partname, namebuflen, "%sp%d", name, partid++);

		blockdesc_t *partdesc = alloc(sizeof(blockdesc_t));
		__assert(partdesc);

		*partdesc = *desc;
		partdesc->lbaoffset = entry->startlba;
		partdesc->blockcapacity = entry->endlba - entry->startlba + 1;
		partdesc->type = BLOCK_TYPE_PART;

		__assert(registerdesc(partdesc, partname) == 0);
	}

	free(tablebuffer);
	free(lba1);
}

static void dombr(blockdesc_t *desc, char *name) {
	// get entries
	void *lba0 = alloc(desc->blocksize);
	__assert(lba0);
	__assert(DISK_READ(desc, lba0, 0, 1) == 0);
	mbrentry_t *mbrents = (mbrentry_t *)((uintptr_t)lba0 + MBR_ENTRIES_OFFSET);

	// name + 'p' + single digit (max mbr partition count is 4) + '\0'
	size_t namebuflen = strlen(name) + 3;
	char partname[namebuflen];
	int partid = 1;

	for (int i = 0; i < 4; ++i) {
		if (mbrents[i].type == MBR_TYPE_FREE)
			continue;

		snprintf(partname, namebuflen, "%sp%d", name, partid++);

		blockdesc_t *partdesc = alloc(sizeof(blockdesc_t));
		__assert(partdesc);

		*partdesc = *desc;
		partdesc->lbaoffset = mbrents[i].lbastart;
		partdesc->blockcapacity = mbrents[i].lbasize;
		partdesc->type = BLOCK_TYPE_PART;

		__assert(registerdesc(partdesc, partname) == 0);
	}

	free(lba0);
}

void block_register(blockdesc_t *desc, char *name) {
	blockdesc_t *permdesc = alloc(sizeof(blockdesc_t));
	__assert(permdesc);
	*permdesc = *desc;

	int part = detectpart(permdesc);

	if (part == PART_GPT)
		dogpt(permdesc, name);

	if (part == PART_MBR)
		dombr(permdesc, name);

	__assert(registerdesc(permdesc, name) == 0);
}

void block_init() {
	hashtable_init(&blocktable, 100);
	MUTEX_INIT(&tablemutex);
}
