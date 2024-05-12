#include <kernel/initrd.h>
#include <limine.h>
#include <string.h>
#include <logging.h>
#include <util.h>
#include <kernel/vmm.h>
#include <kernel/pmm.h>
#include <kernel/abi.h>
#include <time.h>
#include <kernel/vfs.h>

#define TAR_BLOCKSIZE 512
#define TAR_FILE 0
#define TAR_HARDLINK 1
#define TAR_SYMLINK 2
#define TAR_CHARDEV 3
#define TAR_BLOCK 4
#define TAR_DIR 5
#define TAR_FIFO 6

typedef struct {
	char name[100];
	char mode[8];
	char uid[8];
	char gid[8];
	char size[12];
	char modtime[12];
	char checksum[8];
	char type[1];
	char link[100];
	char indicator[6];
	char version[2];
	char uname[32];
	char gname[32];
	char devmajor[8];
	char devminor[8];
	char prefix[155];
} tarheader_t;

typedef struct {
	char name[257];
	mode_t mode;
	gid_t gid;
	uid_t uid;
	size_t size;
	timespec_t modtime;
	short checksum;
	short type;
	char link[101];
	char indicator[6];
	char version[2];
	short devminor;
	short devmajor;
} tarentry_t;

static size_t convert(char *buff, size_t len) {
	size_t result = 0;
	while (len--) {
		result *= 8;
		result += *buff - '0';
		buff++;
	}
	return result;
}

static void buildentry(tarentry_t *entry, void *addr) {
	tarheader_t *header = addr;

	char *namecpyptr = entry->name;
	// name has a prefix
	if (header->prefix[0]) {
		size_t len = header->prefix[154] ? 155 : strlen(header->prefix);
		memcpy(namecpyptr, header->prefix, len);
		namecpyptr += len;
		*namecpyptr++ = '/';
	}
	size_t namelen = header->name[99] ? 99 : strlen(header->name);
	memcpy(namecpyptr, header->name, namelen);
	namecpyptr += namelen;
	*namecpyptr = '\0';

	entry->mode = convert(header->mode, 7);
	entry->uid = convert(header->uid, 7);
	entry->gid = convert(header->gid, 7);
	entry->size = convert(header->size, 11);
	entry->modtime.s = convert(header->modtime, 11);
	entry->modtime.ns = 0;
	entry->checksum = convert(header->checksum, 7);
	entry->type = convert(header->type, 1);
	entry->devmajor = convert(header->devmajor, 7);
	entry->devminor = convert(header->devminor, 7);

	if (header->link[99]) {
		memcpy(entry->link, header->link, 100);
		entry->link[100] = '\0';
	} else {
		strcpy(entry->link, header->link);
	}

	memcpy(entry->indicator, header->indicator, 6);
	memcpy(entry->version, header->version, 2);
}

static volatile struct limine_module_request modreq = {
	.id = LIMINE_MODULE_REQUEST,
	.revision = 0
};

void initrd_unpack() {
	__assert(modreq.response);
	
	struct limine_file *initrd = NULL;
	for (int i = 0; i < modreq.response->module_count; ++i) {
		if (strcmp(modreq.response->modules[i]->path, "/initrd") == 0) {
			initrd = modreq.response->modules[i];
			break;
		}
	}
	__assert(initrd);

	printf("initrd at %p with size %lu (%lu pages)\n", initrd->address, initrd->size, ROUND_UP(initrd->size, PAGE_SIZE) / PAGE_SIZE);
	__assert(((uintptr_t)initrd->address % PAGE_SIZE) == 0);

	void *ptr = initrd->address;
	tarentry_t entry;
	void *cleanupptr = initrd->address;
	size_t cleanupbytespassed = 0;
	for (;;) {
		if (cleanupbytespassed >= PAGE_SIZE) {
			size_t pagec = cleanupbytespassed / PAGE_SIZE;
			pmm_makefree(FROM_HHDM(cleanupptr), pagec);
			cleanupptr = (void *)((uintptr_t)cleanupptr + ROUND_DOWN(cleanupbytespassed, PAGE_SIZE));
			cleanupbytespassed %= PAGE_SIZE;
		}

		buildentry(&entry, ptr);

		if (strncmp("ustar", entry.indicator, 5))
			break;

		vattr_t entryattr;
		entryattr.gid = entry.gid;
		entryattr.uid = entry.uid;
		entryattr.mode = entry.mode;

		void* datastart = (void *)((uintptr_t)ptr + TAR_BLOCKSIZE);
		ptr = (void *)((uintptr_t)datastart + ROUND_UP(entry.size, TAR_BLOCKSIZE));
		cleanupbytespassed += TAR_BLOCKSIZE;

		int err = 0;
		vnode_t *node;
		size_t writecount;
		switch (entry.type) {
			case TAR_FILE:
				cleanupbytespassed += ROUND_UP(entry.size, TAR_BLOCKSIZE);
				err = vfs_create(vfsroot, entry.name, &entryattr, V_TYPE_REGULAR, &node);
				if (err)
					break;

				err = vfs_write(node, datastart, entry.size, 0, &writecount, 0);
				VOP_RELEASE(node);
				break;
			case TAR_DIR:
				err = vfs_create(vfsroot, entry.name, &entryattr, V_TYPE_DIR, NULL);
				break;
			case TAR_SYMLINK:
				err = vfs_link(NULL, entry.link, vfsroot, entry.name, V_TYPE_LINK, &entryattr);
				break;
			default:
				__assert(!"Unsupported file type");
				break;
		}

		if (err)
			printf("initrd: failed to unpack %s: %lu\n", entry.name, err);
	}

	// free remaining pages to be freed
	pmm_makefree(FROM_HHDM(cleanupptr), ROUND_UP(cleanupbytespassed, PAGE_SIZE) / PAGE_SIZE);
}
