#include <kernel/kernel_args.h>
#include <kernel/alloc.h>
#include <hashtable.h>
#include <limine.h>
#include <logging.h>

#define TOTAL_ARGUMENTS_CHARACTER_LIMIT 4096

static volatile struct limine_kernel_file_request kernel_file_request = {
	.id = LIMINE_KERNEL_FILE_REQUEST,
	.revision = 0
};

static size_t allocated = 0;
static char argument_buffer[TOTAL_ARGUMENTS_CHARACTER_LIMIT];

extern kernel_argument_t *kernel_arguments[];
extern kernel_argument_t *kernel_arguments_end[];

static void insert_argument(const char *key, const char *value) {
	size_t argument_count = ((uintptr_t)kernel_arguments_end - (uintptr_t)kernel_arguments) / sizeof(kernel_argument_t *);
	char *buffer = &argument_buffer[allocated];
	if (value) {
		strcpy(buffer, value);
		allocated += strlen(value) + 1;
	}

	for (int i = 0; i < argument_count; ++i) {
		if (strcmp(kernel_arguments[i]->key, key))
			continue;

		kernel_arguments[i]->found = true;

		switch (kernel_arguments[i]->type) {
			case KERNEL_ARGUMENT_TYPE_STR:
				kernel_arguments[i]->value_str = value ? buffer : NULL;
				break;
			case KERNEL_ARGUMENT_TYPE_INT:
				kernel_arguments[i]->value_int = value ? atoll(buffer) : 0;
				break;
		}
	}
}

void kernel_arguments_parse(void) {
	__assert(kernel_file_request.response);
	const char *cmdline = kernel_file_request.response->kernel_file->cmdline;
	__assert(cmdline);
	size_t length = min(strlen(cmdline), TOTAL_ARGUMENTS_CHARACTER_LIMIT);
	char temp_buffer[length];
	memcpy(temp_buffer, cmdline, length);

	bool escaping = false;
	const char *key = temp_buffer[0] == ' ' ? NULL : temp_buffer;
	const char *value = NULL;
	for (int i = 0; i < length; ++i) {
		if (temp_buffer[i] == '"') {
			escaping = !escaping;
			temp_buffer[i] = '\0';
			continue;
		}

		if (temp_buffer[i] == '=') {
			value = &temp_buffer[i + 1];
			temp_buffer[i] = '\0';
			continue;
		}

		if (temp_buffer[i] == ' ' && !escaping) {
			temp_buffer[i] = '\0';

			if (key)
				insert_argument(key, value);

			key = &temp_buffer[i + 1];
			value = NULL;
		}
	}

	if (key != temp_buffer + length)
		insert_argument(key, value);
}
