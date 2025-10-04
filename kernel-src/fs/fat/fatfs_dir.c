#include <kernel/fatfs.h>
#include <logging.h>
#include <util.h>

#define START_OF_LFN 0x40

#define FATFS_DENT_ATTRIBUTE_LONG_NAME 	(FATFS_DENT_ATTRIBUTE_READ_ONLY | \
					FATFS_DENT_ATTRIBUTE_HIDDEN | \
					FATFS_DENT_ATTRIBUTE_SYSTEM | \
					FATFS_DENT_ATTRIBUTE_VOLUME_ID)

#define FATFS_DENT_ATTRIBUTE_LONG_NAME_MASK 	(FATFS_DENT_ATTRIBUTE_READ_ONLY | \
						FATFS_DENT_ATTRIBUTE_HIDDEN | \
						FATFS_DENT_ATTRIBUTE_SYSTEM | \
						FATFS_DENT_ATTRIBUTE_VOLUME_ID | \
						FATFS_DENT_ATTRIBUTE_DIRECTORY | \
						FATFS_DENT_ATTRIBUTE_ARCHIVE)

#define IS_DENT_LFN(x) (((x).attributes & FATFS_DENT_ATTRIBUTE_LONG_NAME_MASK) == FATFS_DENT_ATTRIBUTE_LONG_NAME)

#define FATFS_DENT_NAME_FREE (char)0xe5
#define FATFS_DENT_NAME_FREE_INCLUDING_REST (char)0
#define CHARACTERS_PER_LFN_ENTRY 13

static uint8_t sfn_checksum(const char sfn[11]) {
	// sfn checksum
	uint8_t checksum = 0;
	for (int i = 0; i < 11; ++i)
		checksum = ((checksum & 1) << 7) + (checksum >> 1) + sfn[i];

	return checksum;
}

static int directory_read_dent(fatfs_t *fatfs, fatnode_t *fatnode, fatfs_dent_t *buffer, size_t entry_offset, size_t *entries_read, size_t *disk_offset) {
	if (fatfs->type == FATFS_FAT32 || fatfs->root != fatnode) {
		// valid check since directory size is always block aligned
		size_t byte_offset = entry_offset * sizeof(fatfs_dent_t);
		if (byte_offset >= fatnode->size) {
			*entries_read = 0;
			return 0;	
		}

		int error = fatfs_get_file_disk_offset(fatfs, fatnode, byte_offset, disk_offset);
		if (error)
			return error;

		*entries_read = 1;
		size_t bytes_read;
		error = vfs_read(fatfs->backing, buffer, sizeof(fatfs_dent_t), *disk_offset, &bytes_read, 0);
		if (error)
			return error;

		__assert(bytes_read == sizeof(fatfs_dent_t));
	} else {
		if (entry_offset >= fatfs->root_entry_count) {
			*entries_read = 0;
			return 0;
		}

		*entries_read = 1;
		size_t bytes_read;

		int error = vfs_read(fatfs->backing, buffer, sizeof(fatfs_dent_t), fatfs->root_offset + entry_offset * sizeof(fatfs_dent_t), &bytes_read, 0);
		if (error)
			return error;

		__assert(bytes_read == sizeof(fatfs_dent_t));

		*disk_offset = fatfs->root_offset + entry_offset * sizeof(fatfs_dent_t);
	}

	return 0;
}

static int directory_write_dents(fatfs_t *fatfs, fatnode_t *fatnode, fatfs_dent_t *buffer, size_t entry_offset, size_t entry_count, size_t *disk_offset) {
	if (fatfs->type == FATFS_FAT32 || fatfs->root != fatnode) {
		size_t file_offset = entry_offset * sizeof(fatfs_dent_t);
		int error = fatfs_rw_bytes(fatfs, fatnode, buffer, entry_count * sizeof(fatfs_dent_t), file_offset, true, true);
		if (error)
			return error;

		if (disk_offset)
			error = fatfs_get_file_disk_offset(fatfs, fatnode, file_offset, disk_offset);

		return error;
	} else {
		size_t bytes_written;
		size_t write_offset = fatfs->root_offset + entry_offset * sizeof(fatfs_dent_t);
		int error = vfs_write(fatfs->backing, buffer, entry_count * sizeof(fatfs_dent_t), write_offset, &bytes_written, 0);
		if (error)
			return error;

		if (disk_offset)
			*disk_offset = write_offset;

		__assert(bytes_written == entry_count * sizeof(fatfs_dent_t));
		return 0;
	}
}

// only uppercase and $ % ' - _ @ ~ ` ! ( ) { } ^ # & are valid short name characters
static inline bool valid_sfn_character(char c) {
	if ('A' <= c && c <= 'Z')
		return true;

	if ('0' <= c && c <= '9')
		return true;

	switch (c) {
		case '$':
		case '%':
		case '\'':
		case '-':
		case '_':
		case '@':
		case '~':
		case '`':
		case '!':
		case '(':
		case ')':
		case '{':
		case '}':
		case '^':
		case '#':
		case '&':
			return true;
	}

	return false;
}

static bool str_to_short_filename(const char *str, char short_filename[11]) {
	// first, get the normal name
	int i;
	for (i = 0; i < 8 && str[i] && str[i] != '.'; ++i) {
		if (!valid_sfn_character(str[i]))
			return false;

		short_filename[i] = str[i];
	}

	// not a valid short name if there is no dot in the middle
	if (i == 8 && str[i] != '.' && str[i])
		return false;

	int sfn_current;
	// pad sfn
	for (sfn_current = i; sfn_current < 8; ++sfn_current)
		short_filename[sfn_current] = ' ';

	// if theres a dot, do extension
	if (str[i] == '.') {
		++i;
		while (sfn_current < 11 && str[i]) {
			if (!valid_sfn_character(str[i]))
				return false;

			short_filename[sfn_current] = str[i];

			++i;
			++sfn_current;
		}
	}

	// pad extension
	while (sfn_current < 11)
		short_filename[sfn_current++] = ' ';

	// valid only if we parsed the whole string
	return str[i] == '\0';
}

static bool lfn_get_characters(fatfs_lfn_dent_t *lfn, char *characters) {
	uint16_t buffer[CHARACTERS_PER_LFN_ENTRY];
	for (int i = 0; i < 5; ++i)
		buffer[i] = lfn->name1[i];
	for (int i = 0; i < 6; ++i)
		buffer[i + 5] = lfn->name2[i];

	buffer[11] = lfn->name3[0];
	buffer[12] = lfn->name3[1];

	for (int i = 0; i < 13; ++i) {
		// if any of the upper 9 bits are set then this is not ascii and it is not supported
		if (buffer[i] & 0xff80)
			return false;

		characters[i] = (uint8_t)buffer[i];
	}

	characters[13] = 0;
	return true;
}

static void sfn_get_characters(fatfs_dent_t *dent, char *name_out) {
	int i;

	// name
	for (i = 0; i < 8 && dent->short_name[i] != ' '; ++i)
		name_out[i] = dent->short_name[i];

	// add dot if there is a file extrension
	if (dent->short_name[8] != ' ')
		name_out[i++] = '.';

	// extension
	for (int j = 8; j < 11 && dent->short_name[j] != ' '; ++j)
		name_out[i++] = dent->short_name[j];
}

// if disk_offset_out is NULL then remove the found direntry
int fatfs_directory_lookup(fatfs_t *fatfs, fatnode_t *fatnode, const char *name, fatfs_dent_t *dent_out, size_t *disk_offset_out) {
	fatfs_dent_t dent;
	int error = 0;
	size_t current_dent = 0;
	size_t entries_read;
	int remaining_lfn = -1;
	bool lfn_found = false;
	size_t name_len = strlen(name);
	int lfn_checksum;
	size_t lfn_dent_count = 0;
	size_t disk_offset;

	for (;;) {
		error = directory_read_dent(fatfs, fatnode, &dent, current_dent, &entries_read, &disk_offset);
		if (error)
			goto leave;

		// end of directory?
		if (entries_read == 0 || dent.short_name[0] == FATFS_DENT_NAME_FREE_INCLUDING_REST) {
			error = ENOENT;
			goto leave;
		}

		// this entry in specific is free?
		if (dent.short_name[0] == FATFS_DENT_NAME_FREE) {
			if (remaining_lfn > -1) {
				// free entry while handling LFN, corrupted lfn
			printf("fatfs: corrupted long filename, returning EIO (free while handling lfn)\n");
				error = EIO;
				goto leave;
			}

			++current_dent;
			continue;
		}

		if (IS_DENT_LFN(dent)) {
			// handle LFN
			char lfn_entry[CHARACTERS_PER_LFN_ENTRY + 1];
			fatfs_lfn_dent_t *lfn = (fatfs_lfn_dent_t *)&dent;
			int order = lfn->order;
			size_t lfn_offset = ((lfn->order & ~START_OF_LFN) - 1) * CHARACTERS_PER_LFN_ENTRY;

			if (lfn_offset > 255) {
				printf("fatfs: corrupted long filename, returning EIO (too big)\n");
				error = EIO;
				goto leave;
			}

			if (order & START_OF_LFN) {
				order &= ~START_OF_LFN;

				if (remaining_lfn > -1) {
					// start of lfn while already handling lfn, corrupted
					printf("fatfs: corrupted long filename, returning EIO (bad start)\n");
					error = EIO;
					goto leave;
				}

				// name is larger than lfn? if so, skip this directory entry
				if (name_len >= lfn_offset + CHARACTERS_PER_LFN_ENTRY) {
					current_dent += order + 1;
					continue;
				}

				lfn_checksum = lfn->checksum;
				remaining_lfn = order;
				lfn_dent_count = order;
			} else if (remaining_lfn == -1 || lfn->checksum != lfn_checksum) {
				// not start of lfn while not already handling an lfn, corrupted
				// OR 
				// checksum does not match
				printf("fatfs: corrupted long filename, returing EIO (checksum/bad start)\n");
				error = EIO;
				goto leave;
			}

			lfn_get_characters(lfn, lfn_entry);

			if (strncmp(lfn_entry, name + lfn_offset, CHARACTERS_PER_LFN_ENTRY)) {
				current_dent += remaining_lfn + 1;
				remaining_lfn = -1;
				continue;
			}

			// lfn match
			--remaining_lfn;
			if (remaining_lfn == 0)
				lfn_found = true;

			++current_dent;
			continue;
		} else if (remaining_lfn > 0) {
			// not lfn entry while there are remaining lfns, corrupted
			printf("fatfs: corrupted long filename, returning EIO (non lfn while handling)\n");
			error = EIO;
			goto leave;
		}

		// not a lfn entry
		char short_filename[11];
		bool search_short_filename = str_to_short_filename(name, short_filename);

		// if we parsed an lfn and found this to be the right dir entry, no need to check the short filename

		lfn_dent_count = lfn_found ? lfn_dent_count : 0;

		if (lfn_found == true) {
			if (lfn_checksum != sfn_checksum(dent.short_name)) {
				printf("fatfs: corrupted long filename, returning EIO (bad checksum)\n");
				return EIO;
			}
			lfn_found = false;
			remaining_lfn = -1;
		} else if (!search_short_filename || memcmp(dent.short_name, short_filename, 11)) {
			// does not match
			++current_dent;
			continue;
		}

		// we have found the requested entry
		*dent_out = dent;

		if (disk_offset_out) {
			*disk_offset_out = disk_offset;
		} else {
			// remove the dent
			size_t buffer_size = lfn_dent_count + 1;
			fatfs_dent_t write_buffer[buffer_size];
			uint8_t to_set;

			// first check if this is the last dent
			error = directory_read_dent(fatfs, fatnode, &dent, current_dent + 1, &entries_read, &disk_offset);
			if (error)
				return error;

			if (entries_read == 0 || dent.short_name[0] == FATFS_DENT_NAME_FREE_INCLUDING_REST)
				to_set = FATFS_DENT_NAME_FREE_INCLUDING_REST;
			else
				to_set = FATFS_DENT_NAME_FREE;

			// set the directory entries to the right free value and write the result

			memset(write_buffer, 0, buffer_size * sizeof(fatfs_dent_t));
			for (int i = 0; i < buffer_size; ++i)
				write_buffer[i].short_name[0] = to_set;

			error = directory_write_dents(fatfs, fatnode, write_buffer, current_dent - lfn_dent_count, buffer_size, &disk_offset);
		}

		break;
	}

leave:
	return error;
}

int fatfs_directory_get_dent(fatfs_t *fatfs, fatnode_t *fatnode, size_t dent_offset, char *name_out, fatfs_dent_t *dent_out, size_t *disk_offset) {
	size_t current_dent = 0;
	size_t directory_offset = 0;
	fatfs_dent_t dent;
	size_t entries_read;
	for (;;) {
		int error = directory_read_dent(fatfs, fatnode, &dent, directory_offset, &entries_read, disk_offset);
		if (error)
			return error;

		if (entries_read == 0 || dent.short_name[0] == FATFS_DENT_NAME_FREE_INCLUDING_REST)
			return ENOENT;

		if (dent.short_name[0] == FATFS_DENT_NAME_FREE) {
			++directory_offset;
			continue;
		}

		if (IS_DENT_LFN(dent) && (dent.short_name[0] & START_OF_LFN) == 0) {
			printf("fatfs: corrupted long filename, returning EIO\n");
			return EIO;
		}

		if (dent_offset != current_dent) {
			// not the one we are searching for, keep looking
			fatfs_lfn_dent_t *lfn = (fatfs_lfn_dent_t *)&dent;

			++current_dent;
			directory_offset += IS_DENT_LFN(dent) ? (lfn->order & ~START_OF_LFN) + 1: 1;
			continue;
		}

		break;
	}

	// found the dent we are searching for, get its data
	if (IS_DENT_LFN(dent)) {
		fatfs_lfn_dent_t *lfn = (fatfs_lfn_dent_t *)&dent;
		int limit = lfn->order & ~START_OF_LFN;
		for (int i = 0; i < limit; ++i) {
			if (IS_DENT_LFN(dent) == false) {
				printf("fatfs: corrupted long filename, returning EIO\n");
				return EIO;
			}

			int current_order = lfn->order & ~START_OF_LFN;
			size_t lfn_offset = (current_order - 1) * CHARACTERS_PER_LFN_ENTRY;
			char lfn_name[CHARACTERS_PER_LFN_ENTRY + 1];
			lfn_get_characters(lfn, lfn_name);
			memcpy(name_out + lfn_offset, lfn_name, CHARACTERS_PER_LFN_ENTRY);

			int error = directory_read_dent(fatfs, fatnode, &dent, ++directory_offset, &entries_read, disk_offset);
			if (error)
				return error;

			if (IS_DENT_LFN(dent) && current_order - 1 != lfn->order) {
				printf("fatfs: corrupt long filename, returning EIO\n");
				return EIO;
			}
		}

		// add zero terminator
		name_out[limit * CHARACTERS_PER_LFN_ENTRY] = '\0';
	} else {
		// just get the short filename
		sfn_get_characters(&dent, name_out);
	}

	*dent_out = dent;
	return 0;
}

int fatfs_update_dent(fatfs_t *fatfs, fatnode_t *fatnode) {
	fatfs_dent_t dent;
	size_t bytes_done;

	if (fatfs->type == FATFS_FAT32 && fatnode == fatfs->root) {
		// fat32 root dir, update the start cluster
		int error = vfs_write(fatfs->backing, &fatnode->cluster, sizeof(fatnode->cluster), 44, &bytes_done, 0);
		if (error)
			return error;

		__assert(bytes_done == 4);
		return 0;
	}

	// orphaned, if it is linked again the new dent will have updated information
	if (fatnode->dent_disk_offset == 0)
		return 0;

	int error = vfs_read(fatfs->backing, &dent, sizeof(fatfs_dent_t), fatnode->dent_disk_offset, &bytes_done, 0);
	if (error)
		return error;

	__assert(bytes_done == sizeof(fatfs_dent_t));

	// directories have zero size
	dent.file_size_bytes = fatnode->vnode.type == V_TYPE_DIR ? 0 : fatnode->size;
	dent.cluster_low = fatnode->cluster & 0xffff;
	dent.cluster_high = (fatnode->cluster >> 16) & 0xffff;

	error = vfs_write(fatfs->backing, &dent, sizeof(fatfs_dent_t), fatnode->dent_disk_offset, &bytes_done, 0);
	if (error)
		return error;

	__assert(bytes_done == sizeof(fatfs_dent_t));

	return 0;
}

static int get_free_dents(fatfs_t *fatfs, fatnode_t *fatnode, size_t dent_count, size_t *dent_offset) {
	size_t offset = 0;
	fatfs_dent_t dent;
	size_t free_count = 0;
	bool external_root_dir = fatfs->type != FATFS_FAT32 && fatfs->root == fatnode;

	for (;;) {
		size_t entries_read;
		size_t disk_offset;
		int error = directory_read_dent(fatfs, fatnode, &dent, offset, &entries_read, &disk_offset);
		if (error)
			return error;

		if (entries_read == 0) {
			if (external_root_dir)
				return ENOSPC;

			// grow directory and return ok
			// directories are always cluster size aligned
			size_t grow_count = ROUND_UP((dent_count - free_count) * sizeof(fatfs_dent_t), fatfs->cluster_size);
			error = fatfs_resize_file(fatfs, fatnode, fatnode->size + grow_count);
			*dent_offset = offset - free_count;
			return error;
		}

		if (dent.short_name[0] == FATFS_DENT_NAME_FREE_INCLUDING_REST) {
			// no more dents after this
			size_t dir_dent_count = fatnode->size / sizeof(fatfs_dent_t);
			size_t remaining_dents = dir_dent_count - offset;
			if (remaining_dents >= dent_count) {
				// the remaining dents in the dir are enough to hold this
				*dent_offset = offset;
				break;
			}

			// we still have to grow the directory to have enough space
			free_count = remaining_dents;
			offset += remaining_dents;
			continue;
		}

		if (dent.short_name[0] == FATFS_DENT_NAME_FREE) {
			if (free_count == 0)
				*dent_offset = offset;

			++free_count;
			++offset;

			if (free_count == dent_count)
				break;

			continue;
		}

		free_count = 0;
		offset += IS_DENT_LFN(dent) ? (dent.short_name[0] & ~START_OF_LFN) + 1 : 1;
	}

	return 0;
}

static void set_up_lfns(fatfs_lfn_dent_t *dents, size_t lfn_count, const char *name, const char sfn[11]) {
	size_t len = strlen(name);
	uint8_t checksum = sfn_checksum(sfn);
	
	for (int i = lfn_count - 1; i >= 0; --i) {
		dents[i].order = lfn_count - i;
		dents[i].attr = FATFS_DENT_ATTRIBUTE_LONG_NAME;
		dents[i].reserved = 0;
		dents[i].checksum = checksum;
		dents[i].zero = 0;

		for (int j = 0; j < 5 && *name; ++j)
			dents[i].name1[j] = *name++;

		for (int j = 0; j < 6 && *name; ++j)
			dents[i].name2[j] = *name++;

		for (int j = 0; j < 2 && *name; ++j)
			dents[i].name3[j] = *name++;
	}


	size_t last_dent_null_offset = len % CHARACTERS_PER_LFN_ENTRY;
	if (last_dent_null_offset) {
		if (last_dent_null_offset < 5) {
			dents[0].name1[last_dent_null_offset] = '\0';
		} else if (last_dent_null_offset >= 11) {
			dents[0].name3[last_dent_null_offset - 11] = '\0';
		} else {
			dents[0].name2[last_dent_null_offset - 5] = '\0';
		}
	}

	dents[0].order |= START_OF_LFN;
}

int fatfs_write_directory_entry(fatfs_t *fatfs, fatnode_t *fatnode, fatfs_dent_t *dent, const char *name, size_t *disk_offset) {
	bool is_sfn = str_to_short_filename(name, dent->short_name);
	size_t name_len = strlen(name);
	size_t lfn_count = ROUND_UP(name_len, CHARACTERS_PER_LFN_ENTRY) / CHARACTERS_PER_LFN_ENTRY;

	if (name_len > 255)
		return ENAMETOOLONG;

	size_t dent_offset;
	int error = get_free_dents(fatfs, fatnode, is_sfn ? 1 : (lfn_count + 1), &dent_offset);
	if (error)
		return error;

	size_t file_offset = dent_offset * sizeof(fatfs_dent_t);

	if (is_sfn == false) {
		// generate a short filename
		char tmp_name[13];
		snprintf(tmp_name, 13, "%08d.%03d", dent_offset / 1000, dent_offset % 1000);
		__assert(str_to_short_filename(tmp_name, dent->short_name));

		fatfs_lfn_dent_t lfn_dent_buff[lfn_count];
		size_t lfn_byte_size = lfn_count * sizeof(fatfs_dent_t);

		set_up_lfns(lfn_dent_buff, lfn_count, name, dent->short_name);

		// write the lfn dents
		error = directory_write_dents(fatfs, fatnode, (fatfs_dent_t *)lfn_dent_buff, dent_offset, lfn_count, NULL);
		if (error)
			return error;

		file_offset += lfn_byte_size;
		dent_offset += lfn_count;
	}

	// write the sfn dent
	error = directory_write_dents(fatfs, fatnode, dent, dent_offset, 1, disk_offset);
	if (error)
		return error;

	return error;
}
