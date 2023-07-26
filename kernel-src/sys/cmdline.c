#include <kernel/cmdline.h>
#include <kernel/alloc.h>
#include <hashtable.h>
#include <limine.h>
#include <logging.h>

static volatile struct limine_kernel_file_request kfreq = {
	.id = LIMINE_KERNEL_FILE_REQUEST,
	.revision = 0
};

static hashtable_t pairtable;

char *cmdline_get(char *key) {
	void *v;
	return hashtable_get(&pairtable, &v, key, strlen(key)) == 0 ? v : NULL;
}

void cmdline_parse() {
	__assert(hashtable_init(&pairtable, 16) == 0);
	__assert(kfreq.response);
	struct limine_file *kernelfile = kfreq.response->kernel_file;
	__assert(kernelfile);
	char *cmdline = kernelfile->cmdline;
	__assert(cmdline);

	size_t bufferlen = strlen(cmdline) + 1;
	char buffer[bufferlen];
	memset(buffer, 0, bufferlen);
	char *cmdp = cmdline;
	char *buffp = buffer;

	bool doconvert = true;

	while (*cmdp) {
		char cmdchar = *cmdp++;
		if (cmdchar == ' ' && doconvert)
			*buffp++ = '\0';
		else if (cmdchar == '"') {
			doconvert = !doconvert;
			--bufferlen;
		} else
			*buffp++ = cmdchar;
	}
	buffer[bufferlen] = '\0';

	int i = 0;
	while (i < bufferlen) {
		char *iterator = &buffer[i];
		bool pair = false;
		while (*iterator) {
			if (*iterator == '=') {
				pair = true;
				*iterator = '\0';
			}
			++iterator;
		}

		size_t keylen = strlen(&buffer[i]);
		if (pair) {
			char *valuep = &buffer[i + keylen + 1];
			size_t valuelen = strlen(valuep);

			char *value = alloc(valuelen) + 1;
			__assert(value);
			strcpy(value, valuep);
			__assert(hashtable_set(&pairtable, value, &buffer[i], keylen, true) == 0);
			i += valuelen + 1;
		} else {
			char *value = alloc(keylen + 1);
			__assert(value);
			strcpy(value, &buffer[i]);
			__assert(hashtable_set(&pairtable, value, &buffer[i], keylen, true) == 0);
		}
		i += keylen + 1;
	}
}
