#include <kernel/initrd.h>
#include <kernel/vfs.h>
#include <arch/panic.h>
#include <limine.h>
#include <string.h>
#include <stdio.h>

#define TAR_BLOCKSIZE 512

static volatile struct limine_module_request modreq = {
	.id = LIMINE_MODULE_REQUEST,
	.revision = 0
};

typedef struct{
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
} tar_block;

typedef struct{
	char name[256];
	short mode;
	short gid;
	short uid;
	size_t size;
	size_t modtime;
	short  checksum;
	short  type;
	char link[101];
	char indicator[6];
	char version[2];
	short devminor;
	short devmajor;
} tar_entry;

static volatile size_t convert(char* buff, size_t len){
	size_t result = 0;
	while(len--){
		result *= 8;
		result += *buff - '0';
		buff++;
	}
	
	return result;
}

static volatile void buildentry(tar_entry* entry, void* addr){
	
	tar_block* block = (tar_block*)addr;

	if(block->name[99]){
		if(block->prefix[154]){
			memcpy(entry->name, block->prefix, 154);
		}
		else{
			strcpy(entry->name, block->prefix);
		}
		
		memcpy(entry->name + strlen(entry->name), block->name, 99);

	}
	else{
		
		strcpy(entry->name, block->name);

	}
	
	entry->mode = convert(block->mode, 8);
	entry->uid = convert(block->uid, 8);
	entry->gid = convert(block->gid, 8);
	entry->size = convert(block->size, 11);
	entry->modtime = convert(block->modtime, 11);
	entry->checksum = convert(block->checksum, 8);
	entry->type = convert(block->type, 1);
	
	if(block->link[99]){
		memcpy(entry->link, block->link, 100);
	}
	else{
		strcpy(entry->link, block->link);
	}
	
	memcpy(entry->indicator, block->indicator, 6);
	memcpy(entry->version, block->version, 2);
	entry->devmajor = convert(block->devmajor, 8);
	entry->devminor = convert(block->devminor, 8);

	
	
}

void initrd_parse(){
	
	if(!modreq.response)
		_panic("No modules passed to limine!", 0);
	
	size_t modcount = modreq.response->module_count;
	struct limine_file** modules = modreq.response->modules;
	struct limine_file* initrd = NULL;

	for(size_t i = 0; i < modcount; ++i){	
		if(!strcmp(modules[i]->path, "/initrd")){
			initrd = modules[i];
			break;
		}
	}

	if(!initrd) _panic("No initrd found!", 0);

	printf("Found initrd at %p with size %lu\n", initrd->address, initrd->size);
	
	void* addr = initrd->address;
	
	tar_entry entry;

	memset(entry.name, 0, 256);
	memset(entry.link, 0, 101);

	for(;;){

		buildentry(&entry, addr);	


		if(strncmp("ustar", entry.indicator, 5))
			break;

		printf("NAME: %s SIZE: %lu\n", entry.name, entry.size);
		
		addr += TAR_BLOCKSIZE + TAR_BLOCKSIZE * (entry.size / TAR_BLOCKSIZE) + (entry.size % TAR_BLOCKSIZE > 0 ? TAR_BLOCKSIZE : 0);

	}

}
