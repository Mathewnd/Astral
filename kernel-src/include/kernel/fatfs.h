#ifndef _FATFS_H
#define _FATFS_H

#include <kernel/vfs.h>
#include <hashtable.h>

#define FATFS_FAT12 1
#define FATFS_FAT16 2
#define FATFS_FAT32 3

#define FATFS_CLUSTER_IS_EOF_FAT12(x) ((x) >= 0xff8)
#define FATFS_CLUSTER_IS_EOF_FAT16(x) ((x) >= 0xfff8)
#define FATFS_CLUSTER_IS_EOF_FAT32(x) (((x) & 0x0fffffff) >= 0x0ffffff8)

#define FATFS_CLUSTER_FREE 0

#define FATFS_CLUSTER_EOF_FAT12 0xfff
#define FATFS_CLUSTER_EOF_FAT16 0xffff
#define FATFS_CLUSTER_EOF_FAT32 0xfffffff

#define FATFS_DENT_ATTRIBUTE_READ_ONLY 1
#define FATFS_DENT_ATTRIBUTE_HIDDEN 2
#define FATFS_DENT_ATTRIBUTE_SYSTEM 4
#define FATFS_DENT_ATTRIBUTE_VOLUME_ID 8
#define FATFS_DENT_ATTRIBUTE_DIRECTORY 16
#define FATFS_DENT_ATTRIBUTE_ARCHIVE 32

typedef struct {
	char short_name[11];
	uint8_t attributes;
	uint8_t reserved;
	uint8_t creation_tenth;
	uint16_t creation_time; // 2 second unit
	uint16_t creation_date;
	uint16_t access_date;
	uint16_t cluster_high;
	uint16_t write_time; // 2 second unit
	uint16_t write_date;
	uint16_t cluster_low;
	uint32_t file_size_bytes;
} __attribute__((packed)) fatfs_dent_t;

typedef struct {
	uint8_t order;
	uint16_t name1[5];
	uint8_t attr;
	uint8_t reserved;
	uint8_t checksum;
	uint16_t name2[6];
	uint16_t zero;
	uint16_t name3[2];
} __attribute__((packed)) fatfs_lfn_dent_t;

typedef uint32_t fatfs_cluster_t;

typedef struct fatnode_t {
	vnode_t vnode;
	fatfs_cluster_t cluster;

	size_t size;

	struct fatnode_t *parent_dir;
	size_t dent_disk_offset;
} fatnode_t;

typedef struct {
	vfs_t vfs;
	vnode_t *backing;
	int type;

	union {
		fatfs_cluster_t root_cluster;
		struct {
			size_t root_offset;
			size_t root_entry_count;
		};
	};

	size_t fat_offset;
	size_t data_offset;

	size_t cluster_size;
	size_t cluster_count;

	mutex_t fat_mutex;
	size_t fat_size;
	size_t fat_count;

	mutex_t root_mutex;
	fatnode_t *root;

	mutex_t vnode_map_mutex;
	hashtable_t vnode_map; // <dent disk offset> -> vnode
} fatfs_t;

fatnode_t *fatfs_allocate_node(vfs_t *vfs, int type);
int fatfs_directory_lookup(fatfs_t *fatfs, fatnode_t *fatnode, const char *name, fatfs_dent_t *dent_out, size_t *disk_offset_out); // if disk_offset_out is NULL, remove the dent
int fatfs_directory_get_dent(fatfs_t *fatfs, fatnode_t *fatnode, size_t dent_offset, char *name_out, fatfs_dent_t *dent_out, size_t *disk_offset);
int fatfs_next_cluster(fatfs_t *fatfs, fatfs_cluster_t cluster, fatfs_cluster_t *ret);
bool fatfs_is_cluster_eof(fatfs_t *fs, fatfs_cluster_t cluster);
int fatfs_cluster_chain_length(fatfs_t *fatfs, fatfs_cluster_t cluster, size_t *length);
int fatfs_allocate_cluster(fatfs_t *fatfs, fatfs_cluster_t reference, fatfs_cluster_t *new_cluster);
int fatfs_grow_chain(fatfs_t *fatfs, fatfs_cluster_t chain, size_t grow_count);
void fatfs_free_node(fatnode_t *node);
int fatfs_get_cluster_from_index(fatfs_t *fs, fatnode_t *node, uintmax_t index, fatfs_cluster_t *cluster);
int fatfs_resize_file(fatfs_t *fatfs, fatnode_t *fatnode, size_t new_size);
int fatfs_update_dent(fatfs_t *fatfs, fatnode_t *fatnode);
int fatfs_rw_clusters_iovec(fatfs_t *fs, fatnode_t *node, iovec_iterator_t *iovec_iterator, size_t count, uintmax_t index, bool write, bool cache);
int fatfs_rw_cluster_iovec(fatfs_t *fs, fatnode_t *node, iovec_iterator_t *iovec_iterator, size_t count, uintmax_t offset, uintmax_t index, bool write, bool cache);
int fatfs_rw_bytes_iovec(fatfs_t *fs, fatnode_t *node, iovec_iterator_t *iovec_iterator, size_t count, uintmax_t offset, bool write, bool cache);
int fatfs_rw_bytes(fatfs_t *fs, fatnode_t *node, void *buffer, size_t count, uintmax_t offset, bool write, bool cache);
int fatfs_get_file_disk_offset(fatfs_t *fatfs, fatnode_t *node, size_t byte_offset, size_t *disk_offset);
int fatfs_write_directory_entry(fatfs_t *fatfs, fatnode_t *fatnode, fatfs_dent_t *dent, const char *name, size_t *disk_offset);

#endif
