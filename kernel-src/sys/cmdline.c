#include <kernel/cmdline.h>
#include <kernel/alloc.h>
#include <hashtable.h>
#include <limine.h>
#include <logging.h>

static volatile struct limine_kernel_file_request kernel_file_request = {
	.id = LIMINE_KERNEL_FILE_REQUEST,
	.revision = 0
};

static hashtable_t pair_table;

char *cmdline_get(char *key) {
	void *v;
	return hashtable_get(&pair_table, &v, key, strlen(key)) == 0 ? v : NULL;
}

void cmdline_parse() {
	__assert(hashtable_init(&pair_table, 16) == 0);
	__assert(kernel_file_request.response);
	struct limine_file *kernel_file = kernel_file_request.response->kernel_file;
	__assert(kernel_file);
	char *cmdline = kernel_file->cmdline;
	__assert(cmdline);

	size_t buffer_len = strlen(cmdline) + 1;
	char buffer[buffer_len];
	memset(buffer, 0, buffer_len);
	char *cmdline_ptr = cmdline;
	char *buffer_ptr = buffer;

	bool do_convert = true;

	while (*cmdline_ptr) {
		char cmd_char = *cmdline_ptr++;
		if (cmd_char == ' ' && do_convert)
			*buffer_ptr++ = '\0';
		else if (cmd_char == '"') {
			do_convert = !do_convert;
			--buffer_len;
		} else
			*buffer_ptr++ = cmd_char;
	}
	buffer[buffer_len - 1] = '\0';

	int i = 0;
	while (i < buffer_len) {
		char *iterator = &buffer[i];
		bool pair = false;
		while (*iterator) {
			if (*iterator == '=') {
				pair = true;
				*iterator = '\0';
			}
			++iterator;
		}

		size_t key_len = strlen(&buffer[i]);
		if (pair) {
			char *value_ptr = &buffer[i + key_len + 1];
			size_t value_len = strlen(value_ptr);

			char *value = alloc(value_len) + 1;
			__assert(value);
			strcpy(value, value_ptr);
			__assert(hashtable_set(&pair_table, value, &buffer[i], key_len, true) == 0);
			i += value_len + 1;
		} else {
			char *value = alloc(key_len + 1);
			__assert(value);
			strcpy(value, &buffer[i]);
			__assert(hashtable_set(&pair_table, value, &buffer[i], key_len, true) == 0);
		}
		i += key_len + 1;
	}
}
