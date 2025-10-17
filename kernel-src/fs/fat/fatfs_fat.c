#include <kernel/fatfs.h>
#include <logging.h>
#include <kernel/alloc.h>

static fatfs_cluster_t fatfs_eof_cluster(fatfs_t *fs) {
	switch (fs->type) {
		case FATFS_FAT12:
			return FATFS_CLUSTER_EOF_FAT12;
		case FATFS_FAT16:
			return FATFS_CLUSTER_EOF_FAT16;
		case FATFS_FAT32:
			return FATFS_CLUSTER_EOF_FAT32;
	}

	__assert(!"bad fatfs type");
}

bool fatfs_is_cluster_eof(fatfs_t *fs, fatfs_cluster_t cluster) {
	switch (fs->type) {
		case FATFS_FAT12:
			return FATFS_CLUSTER_IS_EOF_FAT12(cluster);
		case FATFS_FAT16:
			return FATFS_CLUSTER_IS_EOF_FAT16(cluster);
		case FATFS_FAT32:
			return FATFS_CLUSTER_IS_EOF_FAT32(cluster);
	}

	__assert(!"bad fatfs type");
}

static bool fatfs_is_cluster_in_range(fatfs_t *fs, fatfs_cluster_t cluster) {
	return (cluster - 2) < fs->cluster_count;
}

int fatfs_next_cluster(fatfs_t *fatfs, fatfs_cluster_t cluster, fatfs_cluster_t *ret) {
	uint16_t cluster16;
	int error = 0;
	size_t bytes_read = 0;

	switch (fatfs->type) {
		case FATFS_FAT32:
			error = vfs_read(fatfs->backing, ret, 4, fatfs->fat_offset + 4 * cluster, &bytes_read, 0);
			if (error)
				return error;

			__assert(bytes_read == 4);
			break;
		case FATFS_FAT16:
			error = vfs_read(fatfs->backing, &cluster16, 2, fatfs->fat_offset + 2 * cluster, &bytes_read, 0);
			if (error)
				return error;

			__assert(bytes_read == 2);
			*ret = cluster16;
			break;
		case FATFS_FAT12:
			error = vfs_read(fatfs->backing, &cluster16, 2, fatfs->fat_offset + cluster + cluster / 2, &bytes_read, 0);
			if (error)
				return error;

			__assert(bytes_read == 2);
			*ret = ((cluster % 2) ? (cluster16 >> 4) : cluster16) & 0xfff;
			break;
		default:
			__assert(!"bad fatfs type");
	}

	return error;
}

static int set_next_cluster(fatfs_t *fatfs, int fat, fatfs_cluster_t cluster, fatfs_cluster_t next) {
	uint16_t cluster16;
	int error = 0;
	size_t byte_count;

	size_t fat_offset = fatfs->fat_offset + fat * fatfs->fat_size;

	switch (fatfs->type) {
		case FATFS_FAT32:
			error = vfs_write(fatfs->backing, &next, 4, fat_offset + 4 * cluster, &byte_count, 0);
			if (error)
				goto leave;

			__assert(byte_count == 4);
			break;
		case FATFS_FAT16:
			cluster16 = (uint16_t)next;
			error = vfs_write(fatfs->backing, &cluster16, 2, fat_offset + 2 * cluster, &byte_count, 0);
			if (error)
				goto leave;

			__assert(byte_count == 2);
			break;
		case FATFS_FAT12:
			next &= 0xfff;
			error = vfs_read(fatfs->backing, &cluster16, 2, fat_offset + cluster + cluster / 2, &byte_count, 0);
			if (error)
				goto leave;

			__assert(byte_count == 2);
			if (cluster % 2)
				cluster16 = (cluster16 & 0x000f) | (next << 4);
			else
				cluster16 = (cluster16 & 0xf000) | (next & 0xfff);

			error = vfs_write(fatfs->backing, &cluster16, 2, fat_offset + cluster + cluster / 2, &byte_count, 0);
			__assert(byte_count == 2);

			break;
		default:
			__assert(!"bad fatfs type");
	}

	leave:
	return error;
}

int fatfs_set_next_cluster(fatfs_t *fatfs, fatfs_cluster_t cluster, fatfs_cluster_t next, bool lock) {
	if (lock)
		MUTEX_ACQUIRE(&fatfs->fat_mutex);

	int error;
	for (int i = 0; i < fatfs->fat_count; ++i) {
		error = set_next_cluster(fatfs, i, cluster, next);
		if (error)
			break;
	}

	if (lock)
		MUTEX_RELEASE(&fatfs->fat_mutex);

	return error;
}

int fatfs_cluster_chain_length(fatfs_t *fatfs, fatfs_cluster_t cluster, size_t *length) {
	*length = 0;
	if (cluster == 0)
		return 0;
	while (fatfs_is_cluster_eof(fatfs, cluster) == false) {
		int error = fatfs_next_cluster(fatfs, cluster, &cluster);
		if (error)
			return error;

		*length += 1;
	}

	return 0;
}

int fatfs_cut_chain(fatfs_t *fatfs, fatfs_cluster_t last_cluster) {
	fatfs_cluster_t iterator;
	int error = fatfs_next_cluster(fatfs, last_cluster, &iterator);
	if (error)
		return error;

	error = fatfs_set_next_cluster(fatfs, last_cluster, fatfs_eof_cluster(fatfs), true);
	if (error)
		return error;

	while (fatfs_is_cluster_eof(fatfs, iterator) == false) {
		// if fsinfo is ever added, the free cluster count would be incremented here
		fatfs_cluster_t to_free = iterator;

		error = fatfs_next_cluster(fatfs, iterator, &iterator);
		if (error)
			return error;

		error = fatfs_set_next_cluster(fatfs, to_free, FATFS_CLUSTER_FREE, true);
		if (error)
			return error;
	}

	// if fsinfo is ever added, it would be written here

	return 0;
}

int fatfs_grow_chain(fatfs_t *fatfs, fatfs_cluster_t chain, size_t grow_count) {
	if (grow_count == 0)
		return 0;

	fatfs_cluster_t last;
	// find last cluster
	do {
		last = chain;
		int error = fatfs_next_cluster(fatfs, chain, &chain);
		if (error)
			return error;
	} while (chain != fatfs_eof_cluster(fatfs));

	// grow
	int error;
	for (int i = 0; i < grow_count; ++i) {
		fatfs_cluster_t new_cluster;
		// ENOSPC is already handled by fatfs_allocate_cluster
		error = fatfs_allocate_cluster(fatfs, last, &new_cluster);
		if (error)
			return error;

		error = fatfs_set_next_cluster(fatfs, last, new_cluster, true);
		if (error)
			return error;

		last = new_cluster;
	}

	// [sync fsinfo]

	return error;
}

int fatfs_allocate_cluster(fatfs_t *fatfs, fatfs_cluster_t reference, fatfs_cluster_t *new_cluster) {
	int error = 0;
	MUTEX_ACQUIRE(&fatfs->fat_mutex);

	if (reference && fatfs_is_cluster_in_range(fatfs, reference + 1)) {
		fatfs_cluster_t result;
		error = fatfs_next_cluster(fatfs, reference + 1, &result);
		if (error)
			goto leave;

		if (result == FATFS_CLUSTER_FREE) {
			*new_cluster = reference + 1;
			error = fatfs_set_next_cluster(fatfs, reference + 1, fatfs_eof_cluster(fatfs), false);
			// [update fsinfo]
			goto leave;
		}
	}

	fatfs_cluster_t iterator = 2; // [get info from fsinfo]
	*new_cluster = 0;

	while (fatfs_is_cluster_in_range(fatfs, iterator)) {
		fatfs_cluster_t result;
		error = fatfs_next_cluster(fatfs, iterator, &result);
		if (error)
			goto leave;

		if (result == FATFS_CLUSTER_FREE) {
			*new_cluster = iterator;
			break;
		}

		++iterator;
	}

	if (*new_cluster) {
		error = fatfs_set_next_cluster(fatfs, *new_cluster, fatfs_eof_cluster(fatfs), false);
		// [update fsinfo]
	}

	leave:
	MUTEX_RELEASE(&fatfs->fat_mutex);

	if (error == 0 && *new_cluster == 0)
		error = ENOSPC;

	// zero the page if one was allocated
	// TODO use page cache to directly push zero pages
	if (error == 0 && *new_cluster) {
		void *buffer = alloc(fatfs->cluster_size);
		if (buffer == NULL) {
			error = ENOMEM;
			return error;
		}

		size_t bytes_written;
		error = vfs_write(fatfs->backing, buffer, fatfs->cluster_size, fatfs->data_offset + fatfs->cluster_size * (*new_cluster - 2), &bytes_written, V_FFLAGS_NOCACHE);
		__assert(bytes_written == fatfs->cluster_size);

		free(buffer);
	}

	return error;
}

int fatfs_destroy_chain(fatfs_t *fatfs, fatfs_cluster_t cluster) {
		// truncate whole file
		int error = fatfs_cut_chain(fatfs, cluster);
		if (error)
			return error;

		error = fatfs_set_next_cluster(fatfs, cluster, FATFS_CLUSTER_FREE, true);
		if (error)
			return error;

		// [increment and update fsinfo]

		return error;
}

int fatfs_resize_file(fatfs_t *fatfs, fatnode_t *fatnode, size_t new_size) {
	size_t current_cluster_count = ROUND_UP(fatnode->size, fatfs->cluster_size) / fatfs->cluster_size;
	size_t new_cluster_count = ROUND_UP(new_size, fatfs->cluster_size) / fatfs->cluster_size;
	int error;

	if (current_cluster_count == new_cluster_count) {
		fatnode->size = new_size;
		return fatfs_update_dent(fatfs, fatnode);
	}

	if (new_cluster_count > current_cluster_count) {
		// grow
		fatfs_cluster_t cluster;

		// no cluster for this file?
		if (fatnode->cluster == 0) {
			error = fatfs_allocate_cluster(fatfs, 0, &cluster);
			if (error)
				return error;

			++current_cluster_count;
			fatnode->cluster = cluster;

			__assert(cluster);
		}

		error = fatfs_get_cluster_from_index(fatfs, fatnode, current_cluster_count - 1, &cluster);
		if (error)
			return error;

		__assert(cluster != FATFS_CLUSTER_FREE && fatfs_is_cluster_eof(fatfs, cluster) == false);
		error = fatfs_grow_chain(fatfs, fatnode->cluster, new_cluster_count - current_cluster_count);
		if (error == ENOSPC) {
			// try to undo the operation
			fatfs_cut_chain(fatfs, cluster);
			if (fatnode->size == 0) {
				fatfs_set_next_cluster(fatfs, fatnode->cluster, FATFS_CLUSTER_FREE, true);
				fatnode->cluster = 0;
			}
		}
	} else if (new_cluster_count == 0) {
		// truncate whole file
		error = fatfs_destroy_chain(fatfs, fatnode->cluster);
		if (error)
			return error;

		// [update fsinfo]

		fatnode->cluster = 0;
		fatnode->saved_cluster = 0;
	} else {
		// truncate a part of the file
		fatfs_cluster_t cluster;
		error = fatfs_get_cluster_from_index(fatfs, fatnode, new_cluster_count - 1, &cluster);
		if (error)
			return error;

		__assert(cluster != FATFS_CLUSTER_FREE && fatfs_is_cluster_eof(fatfs, cluster) == false);
		error = fatfs_cut_chain(fatfs, cluster);

		// no need to update the saved index/cluster as the previour fatfs_get_cluster_from_index call will have
		// set the index and cluster properly
	}

	if (error == 0) {
		fatnode->size = new_size;
		error = fatfs_update_dent(fatfs, fatnode);
	}

	return error;
}
