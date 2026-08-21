#include <kernel/fatfs.h>
#include <kernel/vfs.h>
#include <util.h>
#include <logging.h>
#include <kernel/alloc.h>
#include <kernel/init.h>
#include <kernel/slab.h>
#include <kernel/auth.h>
#include <kernel/page.h>
#include <kernel/mm.h>

typedef struct {
	uint8_t drive_num;
	uint8_t reserved;
	uint8_t signature; // if 0x29, these fields are valid
	uint32_t serial_number;
	uint8_t label[11];
	uint8_t type[8];
} __attribute__((packed)) ebp_fat16_t;

#define EXTENDED_FLAGS_ACTIVE_FAT_COUNT(x) (((x) & 0xf) + 1)
#define EXTENDED_FLAGS_MIRRORED_FAT 0x80
typedef struct {
	uint32_t fat_size_32;
	uint16_t extended_flags;
	uint16_t version;
	uint32_t root_cluster;
	uint16_t fsinfo_sector;
	uint16_t bootsect;
	uint8_t reserved[12];
	uint8_t drive_num;
	uint8_t reserved2;
	uint8_t signature; // if 0x29, these fields are valid
	uint32_t serial_number;
	uint8_t label[11];
	uint8_t type[8];
} __attribute__((packed)) ebp_fat32_t;

typedef struct {
	uint8_t unused[3];
	uint8_t oem[8];
	uint16_t bytes_per_sector;
	uint8_t sectors_per_cluster;
	uint16_t reserved_sectors;
	uint8_t fat_count;
	uint16_t root_entry_count; // for fat12 and fat16. zero in fat32
	uint16_t sector_count_16; // can be zero, if so use the 32 entry
	uint8_t media;
	uint16_t fat_size_16; // for fat32, use the 32 entry in the fat32 bpb
	uint16_t sectors_per_track;
	uint16_t number_of_heads;
	uint32_t hidden_sectors;
	uint32_t sector_count_32;
	union {
		ebp_fat16_t ebp_fat12;
		ebp_fat16_t ebp_fat16;
		ebp_fat32_t ebp_fat32;
	};
} __attribute__((packed)) bpb_t;

#define FATFS_FSINFO_LEAD_SIG 0x41615252
#define FATFS_FSINFO_SIG 0x61417272
#define FATFS_FSINFO_TRAIL_SIG 0xaA550000

typedef struct {
	uint32_t lead_sig;
	uint8_t reserved[480];
	uint32_t sig;
	uint32_t free_clusters;
	uint32_t next_free;
	uint8_t reserved2[12];
	uint32_t trailing_sig;
} __attribute__((packed)) fatfs_fsinfo_t;

static scache_t *fatnode_cache;

static vfsops_t fatfs_ops;
static int fatfs_mount(vfs_t **vfs, vnode_t *mp, vnode_t *backing, void *data) {
	if (backing == NULL)
		return EINVAL;

	bpb_t bpb;

	size_t read_count;
	int error = vfs_read(backing, &bpb, sizeof(bpb), 0, &read_count, V_FFLAGS_NOCACHE);
	if (error)
		return error;

	if (read_count != sizeof(bpb))
		return EINVAL;

	// do some checks to verify if it is 
	if (bpb.fat_size_16 == 0 && bpb.root_entry_count)
		return EINVAL;

	if (bpb.bytes_per_sector != 512 && bpb.bytes_per_sector != 1024 && bpb.bytes_per_sector != 2048 && bpb.bytes_per_sector != 4096)
		return EINVAL;

	// only valid values are powers of 2 in the range (0, 128]
	bool spc_ok = false;
	for (int i = 1; i <= 128; i *= 2) {
		if (bpb.sectors_per_cluster == i)
			spc_ok = true;
	}
	if (!spc_ok)
		return EINVAL;

	if (bpb.reserved_sectors == 0)
		return EINVAL;

	if (bpb.fat_count == 0)
		return EINVAL;

	// the root directory should be size aligned to a sector
	// this check is still valid in fat32 (0 % n == 0)
	if (bpb.root_entry_count * sizeof(fatfs_dent_t) % bpb.bytes_per_sector != 0)
		return EINVAL;

	if (bpb.fat_size_16 == 0 && bpb.sector_count_16)
		return EINVAL;

	if (bpb.media < 0xf0)
		return EINVAL;

	if (bpb.sector_count_32 == 0 && bpb.sector_count_16 == 0)
		return EINVAL;

	if (bpb.fat_size_16 == 0 && bpb.sector_count_32 == 0)
		return EINVAL;

	vattr_t backing_vattr;
	error = VOP_GETATTR(backing, &backing_vattr, NULL);
	if (error)
		return error;

	fatfs_t *fatfs = alloc(sizeof(fatfs_t));
	if (fatfs == NULL)
		return ENOMEM;

	error = hashtable_init(&fatfs->vnode_map, 4096);
	if (error) {
		free(fatfs);
		return error;
	}

	MUTEX_INIT(&fatfs->root_mutex);
	MUTEX_INIT(&fatfs->vnode_map_mutex);
	MUTEX_INIT(&fatfs->fat_mutex);

	// is this fat32?
	if (bpb.fat_size_16 == 0) {
		fatfs->type = FATFS_FAT32;
		size_t total_sectors = bpb.sector_count_16 ? bpb.sector_count_16 : bpb.sector_count_32;
		size_t data_sectors = total_sectors - bpb.reserved_sectors - bpb.fat_count * bpb.ebp_fat32.fat_size_32;
		fatfs->cluster_count = data_sectors / bpb.sectors_per_cluster;
	} else {
		// to figure out which fat filesystem we are dealing with, use the cluster count
		size_t root_dir_sectors = ROUND_UP(bpb.root_entry_count * 32, bpb.bytes_per_sector) / bpb.bytes_per_sector;
		size_t total_sectors = bpb.sector_count_16 ? bpb.sector_count_16 : bpb.sector_count_32;
		size_t data_sectors = total_sectors - bpb.reserved_sectors - bpb.fat_count * bpb.fat_size_16 - root_dir_sectors;
		fatfs->cluster_count = data_sectors / bpb.sectors_per_cluster;
		fatfs->type = fatfs->cluster_count > 4096 ? FATFS_FAT16 : FATFS_FAT12;
	}

	*vfs = (vfs_t *)fatfs;
	fatfs->vfs.ops = &fatfs_ops;
	fatfs->backing = backing;
	VOP_HOLD(backing);

	fatfs->fat_offset = bpb.reserved_sectors * bpb.bytes_per_sector;
	if (fatfs->type == FATFS_FAT32) {
		fatfs->root_cluster = bpb.ebp_fat32.root_cluster;
		fatfs->fat_size = bpb.ebp_fat32.fat_size_32 * bpb.bytes_per_sector;
		fatfs->data_offset = fatfs->fat_offset + bpb.fat_count * fatfs->fat_size;
	} else {
		fatfs->fat_size = bpb.fat_size_16 * bpb.bytes_per_sector;
		fatfs->root_offset = (bpb.reserved_sectors + bpb.fat_count * bpb.fat_size_16) * bpb.bytes_per_sector;
		fatfs->root_entry_count = bpb.root_entry_count;
		fatfs->data_offset = fatfs->root_offset + ROUND_UP(bpb.root_entry_count * 32, bpb.bytes_per_sector);
	}

	fatfs->fat_count = bpb.fat_count;
	fatfs->cluster_size = bpb.bytes_per_sector * bpb.sectors_per_cluster;

	error = abc_init(&fatfs->abc, backing, bpb.bytes_per_sector);
	if (error) {
		VOP_RELEASE(backing);
		hashtable_destroy(&fatfs->vnode_map);
		free(fatfs);
		return error;
	}

	return error;
}

static int fatfs_unmount(vfs_t *vfs) {
	__assert(!"unimplemented");
}

static int fatfs_sync(vfs_t *vfs) {
	fatfs_t *fatfs = (fatfs_t *)vfs;
	abc_sync(&fatfs->abc);
	int error = mm_cache_sync_vnode(fatfs->backing);
	return error;
}

static int fatfs_root(vfs_t *vfs, vnode_t **root) {
	fatfs_t *fatfs = (fatfs_t *)vfs;
	MUTEX_ACQUIRE(&fatfs->root_mutex);
	int error = 0;
	if (fatfs->root) {
		*root = (vnode_t *)fatfs->root;
		goto leave;
	}

	fatnode_t *fatnode = fatfs_allocate_node(vfs, V_TYPE_DIR);
	if (fatnode == NULL) {
		error = ENOMEM;
		goto leave;
	}

	if (fatfs->type == FATFS_FAT32) {
		fatnode->cluster = fatfs->root_cluster;
		size_t chain_length;
		error = fatfs_cluster_chain_length(fatfs, fatnode->cluster, &chain_length);
		if (error) {
			fatfs_free_node(fatnode);
			goto leave;
		}

		fatnode->size = chain_length * fatfs->cluster_size;
	}

	*root = (vnode_t *)fatnode;
	fatfs->root = fatnode;
	VOP_HOLD(&fatnode->vnode);

	fatnode->vnode.flags |= V_FLAGS_ROOT;

	leave:
	MUTEX_RELEASE(&fatfs->root_mutex);
	return error;
}

static vfsops_t fatfs_ops = {
	.mount = fatfs_mount,
	.unmount = fatfs_unmount,
	.sync = fatfs_sync,
	.root = fatfs_root
};

static void fatfs_init() {
	__assert(vfs_register(&fatfs_ops, "fatfs") == 0);
	fatnode_cache = slab_newcache(sizeof(fatnode_t), 0, NULL, NULL);
	__assert(fatnode_cache);
}

INIT_ROUTINE_DEFINE(fatfs, INIT_ROUTINE_FLAGS_NONE, fatfs_init, vfs);

static int fatfs_syncvnode(vnode_t *vnode) {
	fatfs_t *fs = (fatfs_t *)vnode->vfs;
	fatnode_t *fatnode = (fatnode_t *)vnode;

	// sync file data
	int e = mm_cache_sync_vnode(vnode);
	abc_sync(&fs->abc);

	// sync the dent
	int e2 = 0;
	if (fatnode->dent_disk_offset)
		e2 = mm_cache_sync_vnode(fs->backing);

	// and sync the fats
	int e3 = mm_cache_sync_vnode(fs->backing);

	// only the first errors are reported
	if (e)
		return e;
	if (e2)
		return e2;
	
	return e3;
}

static int fatfs_open(vnode_t **node, int flags, cred_t *cred) {
	return 0;
}

static int fatfs_close(vnode_t *node, int flags, cred_t *cred) {
	return 0;
}

static int fatfs_symlink(vnode_t *parent, char *name, vattr_t *attr, char *path, cred_t *cred) {
	return EPERM;
}

static int fatfs_readlink(vnode_t *parent, char **link, cred_t *cred) {
	return EINVAL;
}

static int fatfs_mmap(vnode_t *node, void *addr, uintmax_t offset, int flags, cred_t *cred) {
	__assert(!"unreachable");
}

static int fatfs_munmap(vnode_t *node, void *addr, uintmax_t offset, int flags, cred_t *cred) {
	__assert(!"unreachable");
}

static int fatfs_access(vnode_t *vnode, mode_t mode, cred_t *cred) {
	return auth_filesystem_check(cred, auth_filesystem_convertaccess(mode), vnode, NULL) ? EACCES : 0;
}

// this *should*, by the rules of the vnode ops, accept a user buffer. however, this will never be the case. all I/O will
// go through the page cache
static int fatfs_write(vnode_t *vnode, iovec_iterator_t *iovec_iterator, size_t size, uintmax_t offset, int flags, size_t *write_count, cred_t *cred) {
	if (vnode->type == V_TYPE_DIR)
		return EISDIR;

	if (vnode->type != V_TYPE_REGULAR)
		return EINVAL;

	fatnode_t *node = (fatnode_t *)vnode;
	fatfs_t *fs = (fatfs_t *)vnode->vfs;
	uintmax_t end_offset = offset + size;
	int error = 0;

	// overflow check
	if (offset > end_offset)
		end_offset = -1ll;

	if (end_offset > node->size) {
		if (offset > node->size) {
			*write_count = 0;
			goto cleanup;
		}

		size = min(end_offset, node->size) - offset;
	}

	error = fatfs_rw_bytes_iovec(fs, node, iovec_iterator, size, offset, true, false);
	*write_count = error ? -1 : size;
	if (error)
		goto cleanup;

	cleanup:
	return error;
}

static int fatfs_read(vnode_t *vnode, iovec_iterator_t *iovec_iterator, size_t size, uintmax_t offset, int flags, size_t *read_count, cred_t *cred) {
	if (vnode->type == V_TYPE_DIR)
		return EISDIR;

	if (vnode->type != V_TYPE_REGULAR)
		return EINVAL;

	fatnode_t *node = (fatnode_t *)vnode;
	fatfs_t *fs = (fatfs_t *)vnode->vfs;
	uintmax_t end_offset = offset + size;
	int error = 0;

	if (offset >= node->size) {
		*read_count = 0;
		goto cleanup;
	}

	// check for overflow
	if (offset > end_offset)
		end_offset = -1ll;

	if (end_offset > node->size) {
		end_offset = node->size;
		size = end_offset - offset;
	}

	if (size == 0)
		goto cleanup;

	error = fatfs_rw_bytes_iovec(fs, node, iovec_iterator, size, offset, false, false);

	*read_count = error ? -1 : size;

	cleanup:
	return error;
}

static int fatfs_putpage(vnode_t *node, uintmax_t offset, struct page_t *page) {
	// only regular files get cached
	__assert(node->type == V_TYPE_REGULAR);
	iovec_t iovec = {
		.addr = MAKE_HHDM(mm_get_page_address(page)),
		.len = PAGE_SIZE
	};

	iovec_iterator_t iovec_iterator;
	iovec_iterator_init(&iovec_iterator, &iovec, 1);
	size_t write_count;
	return VOP_WRITE(node, &iovec_iterator, PAGE_SIZE, offset, 0, &write_count, NULL);
}


static int fatfs_getpage(vnode_t *node, uintmax_t offset, struct page_t *page) {
	// only regular files get cached
	__assert(node->type == V_TYPE_REGULAR);
	size_t read_count = PAGE_SIZE;
	void *addr = MAKE_HHDM(mm_get_page_address(page));

	iovec_t iovec = {
		.addr = addr,
		.len = PAGE_SIZE
	};

	iovec_iterator_t iovec_iterator;
	iovec_iterator_init(&iovec_iterator, &iovec, 1);
	int error = VOP_READ(node, &iovec_iterator, PAGE_SIZE, offset, 0, &read_count, NULL);
	if (read_count == 0)
		return ENXIO;

	else if (read_count != PAGE_SIZE)
		memset((void *)((uintptr_t)addr + read_count), 0, PAGE_SIZE - read_count);

	return error;
}

static int fatfs_lookup(vnode_t *vnode, char *name, vnode_t **result, cred_t *cred) {
	fatnode_t *directory = (fatnode_t *)vnode;
	fatnode_t *new_node = NULL;
	fatfs_t *fatfs = (fatfs_t *)vnode->vfs;

	if (strcmp(name, ".") == 0 || (strcmp(name, "..") == 0 && fatfs->root == directory)) {
		VOP_HOLD(vnode);
		*result = vnode;
		return 0;
	}

	if (strcmp(name, "..") == 0) {
		if (directory->parent_dir == NULL)
			return ENOENT;

		*result = &directory->parent_dir->vnode;
		VOP_HOLD(*result);
		VOP_UNLOCK(vnode);
		VOP_LOCK(*result);
		return 0;
	}

	size_t dent_disk_offset;
	fatfs_dent_t dent;
	int error = fatfs_directory_lookup(fatfs, directory, name, &dent, &dent_disk_offset);
	if (error)
		return error;

	MUTEX_ACQUIRE(&fatfs->vnode_map_mutex);

	// is this file already in memory?
	void *v;
	error = hashtable_get(&fatfs->vnode_map, &v, &dent_disk_offset, sizeof(dent_disk_offset));
	if (error == ENOENT) {
		// it is not, allocate new object
		new_node = fatfs_allocate_node(&fatfs->vfs, (dent.attributes & FATFS_DENT_ATTRIBUTE_DIRECTORY) ? V_TYPE_DIR : V_TYPE_REGULAR);
		if (new_node == NULL) {
			fatfs_free_node(new_node);
			MUTEX_RELEASE(&fatfs->vnode_map_mutex);
			return error;
		}

		// set it in the table
		error = hashtable_set(&fatfs->vnode_map, new_node, &dent_disk_offset, sizeof(dent_disk_offset), true);
		if (error) {
			fatfs_free_node(new_node);
			MUTEX_RELEASE(&fatfs->vnode_map_mutex);
			return error;
		}

		new_node->parent_dir = directory;
		VOP_HOLD(vnode);

		new_node->cluster = ((uint32_t)dent.cluster_high << 16) | dent.cluster_low;

		if (new_node->vnode.type == V_TYPE_DIR) {
			size_t chain_length;
		       	error = fatfs_cluster_chain_length(fatfs, new_node->cluster, &chain_length);
			if (error) {
				fatfs_free_node(new_node);
				MUTEX_RELEASE(&fatfs->vnode_map_mutex);
				return error;
			}

			new_node->size = chain_length * fatfs->cluster_size;
		} else {
			new_node->size = dent.file_size_bytes;
		}

		new_node->dent_disk_offset = dent_disk_offset;

		VOP_HOLD(&new_node->vnode); // table counts as a hold
	} else if (error) {
		MUTEX_RELEASE(&fatfs->vnode_map_mutex);
		return error;
	} else {
		// it is, use the object in the map
		new_node = v;
		VOP_HOLD(&new_node->vnode);
	}

	MUTEX_RELEASE(&fatfs->vnode_map_mutex);

	// VOP_LOOKUP returns the vnode locked
	*result = &new_node->vnode;
	VOP_LOCK(*result);

	return error;
}

static int fatfs_getdents(vnode_t *vnode, dent_t *buffer, size_t count, uintmax_t dir_offset, size_t *read_count) {
	fatfs_t *fatfs = (fatfs_t *)vnode->vfs;
	fatnode_t *node = (fatnode_t *)vnode;

	if (vnode->type != V_TYPE_DIR)
		return ENOTDIR;

	int i;
	int error = 0;
	for (i = 0; i < count; ++i) {
		fatfs_dent_t dent;
		size_t disk_offset;
		error = fatfs_directory_get_dent(fatfs, node, dir_offset + i, buffer[i].d_name, &dent, &disk_offset);
		if (error == ENOENT) {
			error = 0;
			break;
		} else if (error) {
			break;
		}

		buffer[i].d_ino = disk_offset / sizeof(fatfs_dent_t);
		buffer[i].d_off = dir_offset + i;
		buffer[i].d_reclen = sizeof(dent_t);
		buffer[i].d_type = vfs_getposixtype((dent.attributes & FATFS_DENT_ATTRIBUTE_DIRECTORY) ? V_TYPE_DIR : V_TYPE_REGULAR);
	}


	*read_count = i;
	return error;
}

static int fatfs_getattr(vnode_t *vnode, vattr_t *attr, cred_t *cred) {
	fatnode_t * fatnode = (fatnode_t *)vnode;
	fatfs_t *fatfs = (fatfs_t *)vnode->vfs;
	attr->rdevmajor = 0;
	attr->rdevminor = 0;

	vattr_t backing_attr;

	VOP_LOCK(fatfs->backing);
	int error = VOP_GETATTR(fatfs->backing, &backing_attr, cred);
	VOP_UNLOCK(fatfs->backing);
	if (error)
		return error;

	attr->devmajor = backing_attr.rdevmajor;
	attr->devminor = backing_attr.rdevminor;
	attr->uid = 0;
	attr->gid = 0;
	attr->inode = fatnode->dent_disk_offset / 32;
	attr->type = fatnode->vnode.type;
	attr->mode = 0755;
	attr->nlinks = fatnode->parent_dir ? 1 : 0;
	attr->size = fatnode->size;
	attr->fsblocksize = fatfs->cluster_size;
	// TODO times
	attr->atime.s = 0;
	attr->atime.ns = 0;
	attr->ctime.s = 0;
	attr->ctime.ns = 0;
	attr->mtime.s = 0;
	attr->mtime.ns = 0;
	attr->blocksused = ROUND_UP(attr->size, attr->fsblocksize) / attr->fsblocksize;
	return 0;
}

static int fatfs_setattr(vnode_t *vnode, vattr_t *attr, int which, cred_t *cred) {
	return 0;
}

static int fatfs_create(vnode_t *parent, char *name, vattr_t *attr, int type, vnode_t **result, cred_t *cred) {
	fatnode_t *node = (fatnode_t *)parent;
	fatfs_t *fatfs = (fatfs_t *)parent->vfs;
	if (type != V_TYPE_REGULAR && type != V_TYPE_DIR)
		return EPERM;

	if (parent->type != V_TYPE_DIR)
		return ENOTDIR;

	fatnode_t *new_node = fatfs_allocate_node(parent->vfs, type);
	if (new_node == NULL)
		return ENOMEM;

	fatfs_dent_t dent;
	size_t dent_offset;
	int error = fatfs_directory_lookup(fatfs, node, name, &dent, &dent_offset);
	if (error == 0)
		error = EEXIST;

	if (error != ENOENT)
		goto cleanup;

	memset(&dent, 0, sizeof(dent));
	dent.attributes = (type == V_TYPE_DIR) ? FATFS_DENT_ATTRIBUTE_DIRECTORY : 0;

	size_t disk_offset;
	error = fatfs_write_directory_entry(fatfs, node, &dent, name, &disk_offset);
	if (error)
		goto cleanup;

	MUTEX_ACQUIRE(&fatfs->vnode_map_mutex);
	error = hashtable_set(&fatfs->vnode_map, new_node, &disk_offset, sizeof(disk_offset), true);

	if (!error) {
		new_node->parent_dir = node;
		VOP_HOLD(parent);
		new_node->dent_disk_offset = disk_offset;
	}

	MUTEX_RELEASE(&fatfs->vnode_map_mutex);

	if (error)
		goto cleanup;

	// table hold
	VOP_HOLD(&new_node->vnode);
	*result = &new_node->vnode;

	// create . and .. entries if directory
	if (type == V_TYPE_DIR) {
		fatfs_dent_t dot_dents[2];
		memset(&dot_dents, 0, sizeof(dot_dents));

		int e = fatfs_resize_file(fatfs, new_node, fatfs->cluster_size);
		if (e == 0) {
			memcpy(dot_dents[0].short_name, ".          ", 11);
			memcpy(dot_dents[1].short_name, "..         ", 11);

			dot_dents[0].cluster_low = new_node->cluster & 0xffff;
			dot_dents[0].cluster_high = (new_node->cluster >> 16) & 0xffff;

			if (node != fatfs->root) {
				dot_dents[1].cluster_low = node->cluster & 0xffff;
				dot_dents[1].cluster_high = (node->cluster >> 16) & 0xffff;
			}

			dot_dents[0].attributes = dot_dents[1].attributes = FATFS_DENT_ATTRIBUTE_DIRECTORY;

			fatfs_rw_bytes(fatfs, new_node, &dot_dents, sizeof(dot_dents), 0, true, true);
			fatfs_update_dent(fatfs, new_node);
		}
	}

	// VOP_CREATE expects result to be locked
	VOP_LOCK(*result);

	cleanup:
	if (error)
		fatfs_free_node(new_node);

	return error;
}

static int is_directory_empty(fatnode_t *node) {
	// check if its actually empty (readcount > 2, asserted 2 for . and ..)
	size_t readcount;
	dent_t *dents = alloc(sizeof(dent_t) * 3);
	if (dents == NULL)
		return ENOMEM;

	int error = fatfs_getdents((vnode_t *)node, dents, 3, 0, &readcount);
	if (error) {
		free(dents);
		return error;
	}

	if (readcount > 2) {
		free(dents);
		return ENOTEMPTY;
	}

	// if any of the dot entries is missing, do not do anything as we cannot be sure of consistency
	if (readcount != 2 ||
	strcmp(dents[0].d_name, ".") != 0 || strcmp(dents[1].d_name, "..") != 0) {
		free(dents);
		printf("fatfs: filesystem corruption detected: missing or bad dot entries\n");
		return ENOTEMPTY;
	}

	free(dents);

	return 0;
}

static int fatfs_rename(vnode_t *vsource_dir, vnode_t *vsource, char *old_name, vnode_t *vtarget_dir, char *new_name, int flags) {
	fatfs_t *fatfs = (fatfs_t *)vsource_dir->vfs;
	fatnode_t *source_dir = (fatnode_t *)vsource_dir;
	fatnode_t *source = (fatnode_t *)vsource;
	fatnode_t *target_dir = (fatnode_t *)vtarget_dir;

	if (vsource_dir->vfs != vtarget_dir->vfs)
		return EXDEV;

	if (vsource_dir->type != V_TYPE_DIR || vtarget_dir->type != V_TYPE_DIR)
		return ENOTDIR;

	if (vsource->vfsmounted)
		return EBUSY;

	vnode_t *vtarget = NULL;
	fatnode_t *target;

	int error = fatfs_lookup(vtarget_dir, new_name, &vtarget, NULL);
	if (error && error != ENOENT)
		return error;

	target = (fatnode_t *)vtarget;

	if (target) {
		if (flags & RENAME_NOREPLACE) {
			VOP_UNLOCK(vtarget);
			VOP_RELEASE(vtarget);
			return EEXIST;
		}

		// we are replacing an existing link
		// do some checks expected by posix
		if (vsource->type != V_TYPE_DIR && vtarget->type == V_TYPE_DIR) {
			VOP_UNLOCK(vtarget);
			VOP_RELEASE(vtarget);
			return EISDIR;
		}

		if (vsource->type == V_TYPE_DIR && vtarget->type != V_TYPE_DIR) {
			VOP_UNLOCK(vtarget);
			VOP_RELEASE(vtarget);
			return ENOTDIR;
		}

		if (vtarget->vfsmounted) {
			VOP_UNLOCK(vtarget);
			VOP_RELEASE(vtarget);
			return EBUSY;
		}

		error = vtarget->type == V_TYPE_DIR ? is_directory_empty(target) : 0;
		if (error) {
			VOP_UNLOCK(vtarget);
			VOP_RELEASE(vtarget);
			return error;
		}

		if (source == target) {
			// POSIX says that if both are the same file, rename is a no-op
			VOP_UNLOCK(vtarget);
			VOP_RELEASE(vtarget);
			return 0;
		}
	}

	fatfs_dent_t new_dent = {
		.attributes = (vsource->type == V_TYPE_DIR) ? FATFS_DENT_ATTRIBUTE_DIRECTORY : 0,
		.cluster_low = source->cluster & 0xffff,
		.cluster_high = (source->cluster >> 16) & 0xffff,
		.file_size_bytes = source->size
	};

	// switch out or create the target dent
	fatfs_dent_t target_dent;
	size_t dent_disk_offset;
	error = fatfs_directory_lookup(fatfs, target_dir, new_name, &target_dent, &dent_disk_offset);
	if (error == ENOENT) {
		__assert(target == NULL);
		// create dirent
		error = fatfs_write_directory_entry(fatfs, target_dir, &new_dent, new_name, &dent_disk_offset);
		if (error)
			return error;
	} else if (error == 0) {
		memcpy(new_dent.short_name, target_dent.short_name, 11);

		// already exists, switch it up
		error = fatfs_disk_rw(fatfs, &new_dent, sizeof(new_dent), dent_disk_offset, true, true);
		if (error) {
			VOP_UNLOCK(vtarget);
			VOP_RELEASE(vtarget);
			return error;
		}

		MUTEX_ACQUIRE(&fatfs->vnode_map_mutex);
		// remove from the vnode map
		__assert(hashtable_remove(&fatfs->vnode_map, &dent_disk_offset, sizeof(dent_disk_offset)) == 0);
		MUTEX_RELEASE(&fatfs->vnode_map_mutex);

		VOP_RELEASE(vtarget); // table hold

		__assert(target->parent_dir == target_dir);
		VOP_RELEASE(vtarget_dir);
		target->parent_dir = NULL;
		target->dent_disk_offset = 0;

		VOP_UNLOCK(vtarget);
		VOP_RELEASE(vtarget); // lookup hold
	} else {
		if (vtarget) {
			VOP_UNLOCK(vtarget);
			VOP_RELEASE(vtarget);
		}
		return error;
	}

	MUTEX_ACQUIRE(&fatfs->vnode_map_mutex);
	__assert(hashtable_remove(&fatfs->vnode_map, &source->dent_disk_offset, sizeof(source->dent_disk_offset)) == 0);

	// TODO add a way to preallocate hashtable enties to prevent filesystem corruption
	error = hashtable_set(&fatfs->vnode_map, source, &dent_disk_offset, sizeof(dent_disk_offset), true);
	if (!error) {
		__assert(source->parent_dir == source_dir);
		VOP_RELEASE(vsource_dir);
		source->parent_dir = target_dir;
		VOP_HOLD(vtarget_dir);

		source->dent_disk_offset = dent_disk_offset;
	}

	MUTEX_RELEASE(&fatfs->vnode_map_mutex);

	if (error)
		return error;

	// table hold
	VOP_HOLD(&source->vnode);

	// remove old dent
	return fatfs_directory_lookup(fatfs, source_dir, old_name, &target_dent, NULL);
}

static int fatfs_link(vnode_t *vnode, vnode_t *dir, char *name, cred_t *cred) {
	fatnode_t *node = (fatnode_t *)dir;
	fatfs_t *fatfs = (fatfs_t *)vnode->vfs;
	fatnode_t *child_node = (fatnode_t *)vnode;

	if (vnode->vfs != dir->vfs)
		return EXDEV;

	// fatfs only supports only one hard link per file
	if (child_node->parent_dir)
		return EMLINK;

	fatfs_dent_t dent;
	size_t dent_offset;
	int error = fatfs_directory_lookup(fatfs, node, name, &dent, &dent_offset);
	if (error == 0)
		return EEXIST;
	else if (error != ENOENT)
		return error;

	memset(&dent, 0, sizeof(dent));
	//dent.attributes = child_node->vnode.type == V_TYPE_DIR ? FATFS_DENT_ATTRIBUTE_DIRECTORY : 0;
	//dent.cluster_high = (child_node->cluster & 0xffff) << 16;
	//dent.cluster_low  = (child_node->cluster >> 16) & 0xffff;
	//dent.file_size_bytes = child_node->size;
	size_t disk_offset;
	error = fatfs_write_directory_entry(fatfs, node, &dent, name, &disk_offset);
	if (error)
		return error;

	MUTEX_ACQUIRE(&fatfs->vnode_map_mutex);
	// TODO add a mechanism to preallocate hashtable entry data to prevent failure here
	// when that is done, re-add the dent init code above
	error = hashtable_set(&fatfs->vnode_map, child_node, &disk_offset, sizeof(disk_offset), true);

	if (!error) {
		child_node->parent_dir = node;
		VOP_HOLD(dir);
		child_node->dent_disk_offset = disk_offset;
		fatfs_update_dent(fatfs, child_node);
	}

	MUTEX_RELEASE(&fatfs->vnode_map_mutex);

	if (error)
		return error;

	// table hold
	VOP_HOLD(vnode);
	return error;
}

static int fatfs_unlink(vnode_t *vnode, vnode_t *child, char *name, cred_t *cred) {
	fatnode_t *node = (fatnode_t *)vnode;
	fatfs_t *fs = (fatfs_t *)vnode->vfs;
	fatnode_t *child_fatnode = (fatnode_t *)child;

	int error = child->type == V_TYPE_DIR ? is_directory_empty(child_fatnode) : 0;
	if (error)
		return error;

	// remove the dent
	fatfs_dent_t dent;
	error = fatfs_directory_lookup(fs, node, name, &dent, NULL);
	if (error)
		return error;

	// remove from the vnode map
	MUTEX_ACQUIRE(&fs->vnode_map_mutex);
	__assert(hashtable_remove(&fs->vnode_map, &child_fatnode->dent_disk_offset, sizeof(child_fatnode->dent_disk_offset)) == 0);
	MUTEX_RELEASE(&fs->vnode_map_mutex);
	VOP_RELEASE(child); // table hold

	__assert(child_fatnode->parent_dir == node);
	VOP_RELEASE(vnode);
	child_fatnode->parent_dir = NULL;
	child_fatnode->dent_disk_offset = 0;

	return 0;
}

static int fatfs_resize(vnode_t *vnode, size_t new_size, cred_t *cred) {
	if (vnode->type == V_TYPE_DIR)
		return EISDIR;

	if (vnode->type != V_TYPE_REGULAR)
		return EINVAL;

	__assert(vnode->type == V_TYPE_REGULAR);
	fatnode_t *node = (fatnode_t *)vnode;
	fatfs_t *fs = (fatfs_t *)vnode->vfs;

	int error = 0;
	size_t old_size = node->size;
	if (old_size != new_size) {
		error = fatfs_resize_file(fs, node, new_size);
		if (error == 0 && old_size > new_size)
			mm_cache_truncate(vnode, new_size);
	}

	return error;
}

static int fatfs_inactive(vnode_t *vnode) {
	fatnode_t *fatnode = (fatnode_t *)vnode;
	fatfs_t *fatfs = (fatfs_t *)vnode->vfs;

	if (fatnode->parent_dir) {
		// TODO there is still no *normal* situation where this would happen, so it has 
		// not been implemented yet. the node still has links on the filesystem but is being 
		// released from memory due to only being referenced by the vnode map. 
		// this was done likely to free up memory (and the structures and data will still remain on disk)
		// the vnode map mutex is nescessarily being held here by the function that called VOP_RELEASE
		__assert(!"fatfs VOP_INACTIVE() with hardlink count >0 is currently unimplemented");
	} else {
		// unlinked file having resources released when kernel refcount hits zero
		fatfs_resize_file(fatfs, fatnode, 0);
	}

	fatfs_free_node(fatnode);
	return 0;
}

static int fatfs_advlock(vnode_t *node, int op, advlock_t *advlock) {
	return vfs_advlock(node, op, advlock);
}

static int fatfs_lock(vnode_t *node) {
	MUTEX_ACQUIRE(&node->lock);
	return 0;
}

static int fatfs_unlock(vnode_t *node) {
	MUTEX_RELEASE(&node->lock);
	return 0;
}

static vops_t fatfs_vops = {
	.write = fatfs_write,
	.read = fatfs_read,
	.getdents = fatfs_getdents,
	.lookup = fatfs_lookup,
	.getattr = fatfs_getattr,
	.setattr = fatfs_setattr,
	.open = fatfs_open,
	.close = fatfs_close,
	.poll = vfs_pollstub,
	.access = fatfs_access,
	.readlink = fatfs_readlink,
	.resize = fatfs_resize,
	.link = fatfs_link,
	.create = fatfs_create,
	.symlink = fatfs_symlink,
	.mmap = fatfs_mmap,
	.munmap = fatfs_munmap,
	.unlink = fatfs_unlink,
	.inactive = fatfs_inactive,
	.rename = fatfs_rename,
	.getpage = fatfs_getpage,
	.putpage = fatfs_putpage,
	.sync = fatfs_syncvnode,
	.advlock = fatfs_advlock,
	.lock = fatfs_lock,
	.unlock = fatfs_unlock
};

void fatfs_free_node(fatnode_t *node) {
	slab_free(fatnode_cache, node);
}

fatnode_t *fatfs_allocate_node(vfs_t *vfs, int type) {
	fatnode_t *fatnode = slab_allocate(fatnode_cache);
	if (fatnode == NULL)
		return fatnode;

	VOP_INIT(&fatnode->vnode, &fatfs_vops, 0, type, vfs);

	fatnode->cluster = 0;
	fatnode->size = 0;
	fatnode->saved_index = 0;
	fatnode->saved_cluster = 0;
	fatnode->parent_dir = NULL;
	fatnode->dent_disk_offset = 0;

	return fatnode;
}
