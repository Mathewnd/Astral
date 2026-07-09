#include <kernel/fatfs.h>
#include <logging.h>

int fatfs_get_cluster_from_index(fatfs_t *fs, fatnode_t *node, uintmax_t index, fatfs_cluster_t *cluster) {
	if (node->saved_cluster == 0 || node->saved_index > index) {
		node->saved_cluster = node->cluster;
		node->saved_index = 0;
	}

	fatfs_cluster_t iterator = node->saved_cluster;

	// empty file?
	if (iterator == 0) {
		// return EOF
		switch (fs->type) {
			case FATFS_FAT12:
				*cluster = FATFS_CLUSTER_EOF_FAT12;
				break;
			case FATFS_FAT16:
				*cluster = FATFS_CLUSTER_EOF_FAT16;
				break;
			case FATFS_FAT32:
				*cluster = FATFS_CLUSTER_EOF_FAT32;
				break;
		}

		return 0;
	}

	while (index--) {
		int error = fatfs_next_cluster(fs, iterator, &iterator);
		if (error)
			return error;

		// we set iterator to the specific EOF cluster because many different values can
		// signal an EOF (example: 0xff8 to 0xfff for fat12)
		if (fs->type == FATFS_FAT12 && FATFS_CLUSTER_IS_EOF_FAT12(iterator)) {
			iterator = FATFS_CLUSTER_EOF_FAT12;
			break;
		} else if (fs->type == FATFS_FAT16 && FATFS_CLUSTER_IS_EOF_FAT16(iterator)) {
			iterator = FATFS_CLUSTER_EOF_FAT16;
			break;
		} else if (fs->type == FATFS_FAT32 && FATFS_CLUSTER_IS_EOF_FAT32(iterator)) {
			iterator = FATFS_CLUSTER_EOF_FAT32;
			break;
		}
	}

	if (fatfs_is_cluster_eof(fs, iterator) == false) {
		// update the last found index
		node->saved_index = index;
		node->saved_cluster = iterator;
	}

	*cluster = iterator;
	return 0;
}

static inline size_t cluster_disk_offset(fatfs_t *fs, fatfs_cluster_t cluster) {
	return fs->data_offset + (cluster - 2) * fs->cluster_size;
}

int fatfs_get_file_disk_offset(fatfs_t *fs, fatnode_t *node, size_t byte_offset, size_t *disk_offset) {
	__assert(byte_offset < node->size);

	fatfs_cluster_t cluster;
	int error = fatfs_get_cluster_from_index(fs, node, byte_offset / fs->cluster_size, &cluster);
	if (error)
		return error;

	*disk_offset = cluster_disk_offset(fs, cluster) + byte_offset % fs->cluster_size;
	return 0;
}

int fatfs_rw_clusters_iovec(fatfs_t *fs, fatnode_t *node, iovec_iterator_t *iovec_iterator, size_t count, uintmax_t index, bool write, bool cache) {
	fatfs_cluster_t cluster;
	int error = fatfs_get_cluster_from_index(fs, node, index, &cluster);
	if (error)
		return error;

	__assert(cluster && fatfs_is_cluster_eof(fs, cluster) == false);

	uintmax_t i = 0;
	while (i < count) {
		__assert(index + i < ROUND_UP(node->size, fs->cluster_size) / fs->cluster_size);

		size_t cluster_count = 1;
		fatfs_cluster_t last_cluster = cluster;
		fatfs_cluster_t next_loop_cluster = 0;
		while (i + cluster_count < count) {
			fatfs_cluster_t next_cluster;
			error = fatfs_next_cluster(fs, last_cluster, &next_cluster);
			if (error)
				return error;

			__assert(next_cluster);
			__assert(fatfs_is_cluster_eof(fs, next_cluster) == false);

			if (next_cluster != last_cluster + 1) {
				next_loop_cluster = next_cluster;
				break;
			}

			last_cluster = next_cluster;
			cluster_count += 1;
		}

		uintmax_t disk_offset = cluster_disk_offset(fs, cluster);
		size_t byte_count = cluster_count * fs->cluster_size;
		size_t done_count;
		error = write ?
			vfs_write_iovec(fs->backing, iovec_iterator, byte_count, disk_offset, &done_count, cache ? 0 : V_FFLAGS_NOCACHE) :
			vfs_read_iovec(fs->backing, iovec_iterator, byte_count, disk_offset, &done_count, cache ? 0 : V_FFLAGS_NOCACHE);

		if (error)
			return error;

		__assert(done_count == byte_count);
		i += cluster_count;

		cluster = next_loop_cluster;
	}

	return 0;
}

int fatfs_rw_cluster_iovec(fatfs_t *fs, fatnode_t *node, iovec_iterator_t *iovec_iterator, size_t count, uintmax_t offset, uintmax_t index, bool write, bool cache) {
	__assert(offset + count <= fs->cluster_size);
	__assert(index < ROUND_UP(node->size, fs->cluster_size) / fs->cluster_size);

	fatfs_cluster_t cluster;
	int error = fatfs_get_cluster_from_index(fs, node, index, &cluster);
	if (error)
		return error;

	__assert(cluster && fatfs_is_cluster_eof(fs, cluster) == false);

	uintmax_t disk_offset = cluster_disk_offset(fs, cluster) + offset;
	size_t done_count;
	error = write ?
		vfs_write_iovec(fs->backing, iovec_iterator, count, disk_offset, &done_count, cache ? 0 : V_FFLAGS_NOCACHE) :
		vfs_read_iovec(fs->backing, iovec_iterator, count, disk_offset, &done_count, cache ? 0 : V_FFLAGS_NOCACHE);

	if (error)
		return error;

	__assert(done_count == count);
	return error;
}

int fatfs_rw_bytes_iovec(fatfs_t *fs, fatnode_t *node, iovec_iterator_t *iovec_iterator, size_t count, uintmax_t offset, bool write, bool cache) {
	uintmax_t index = offset / fs->cluster_size;
	uintmax_t start_offset = offset % fs->cluster_size;

	// r/w the first cluster if there's an offset into it
	if (start_offset) {
		size_t block_remaining = fs->cluster_size - start_offset;
		size_t do_count = min(block_remaining, count);
		int error = fatfs_rw_cluster_iovec(fs, node, iovec_iterator, do_count, start_offset, index, write, cache);
		if (error)
			return error;

		count -= do_count;
		index += 1;
	}

	// r/w all middle clusters
	size_t blocks = count / fs->cluster_size;
	if (blocks) {
		int error = fatfs_rw_clusters_iovec(fs, node, iovec_iterator, blocks, index, write, cache);
		if (error)
			return error;

		count -= blocks * fs->cluster_size;
		index += blocks;
	}

	// r/w remaining cluster if there's any data left and return
	return count ? fatfs_rw_cluster_iovec(fs, node, iovec_iterator, count, 0, index, write, cache) : 0;
}

int fatfs_rw_bytes(fatfs_t *fs, fatnode_t *node, void *buffer, size_t count, uintmax_t offset, bool write, bool cache) {
	iovec_t iovec = {
		.addr = buffer,
		.len = count
	};

	iovec_iterator_t iovec_iterator;
	iovec_iterator_init(&iovec_iterator, &iovec, 1);

	return fatfs_rw_bytes_iovec(fs, node, &iovec_iterator, count, offset, write, cache);
}
