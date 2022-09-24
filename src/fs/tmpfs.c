#include <kernel/tmpfs.h>
#include <kernel/vfs.h>
#include <kernel/alloc.h>
#include <arch/mmu.h>
#include <kernel/pmm.h>
#include <string.h>
#include <dirent.h>


static int tmpfs_mount(dirnode_t* mountpoint, vnode_t* device, int mountflags, void* fsinfo){
	
	fs_t* desc = alloc(sizeof(fs_t));
	
	if(!desc)
		return ENOMEM;

	dirnode_t* root = vfs_newdirnode(mountpoint->vnode.name, desc, NULL, mountpoint->vnode.parent);

	if(!root){
		free(desc);
		return ENOMEM;
	}

	desc->calls = tmpfs_getfuncs();
	mountpoint->mount = root;
	root->vnode.st.st_mode = MAKETYPE(TYPE_DIR) | 0777;

	return 0;
}

static int tmpfs_write(int* error, vnode_t* node, void* buff, size_t count, size_t offset){
	
	// TODO use virtual memory

	size_t targetpage = (count + offset) / PAGE_SIZE;

	if(node->st.st_blocks <= targetpage){
		void* new = pmm_hhdmalloc(targetpage+1);
		if(!new){
			*error = ENOMEM;
			return -1;
		};
		memcpy(new, node->fsdata, node->st.st_size);
		pmm_free(node->fsdata, node->st.st_size / PAGE_SIZE + 1);
		node->fsdata = new;
		node->st.st_blocks = targetpage+1;
		node->st.st_size = count + offset;
	}

	memcpy(node->fsdata + offset, buff, count);

	if(offset + count > node->st.st_size)
		node->st.st_size = offset + count;

	*error = 0;
	return count;
}

static int tmpfs_read(int *error, vnode_t* node, void* buff, size_t count, size_t offset){
	
	size_t sizemax = node->st.st_size - offset;
	
	if(count > sizemax)
		count = sizemax;
	
	memcpy(buff, node->fsdata + offset, count);
	*error = 0;

	return count;
	
}

static int tmpfs_unmount(fs_t* fs) UNIMPLEMENTED
static int tmpfs_open(dirnode_t* parent, char* name){ return ENOENT;} // tmpfs open should never be called if a file exists
static int tmpfs_close(vnode_t* node){return 0;} // nothing to do really

static int tmpfs_mkdir(dirnode_t* parent, char* name, mode_t mode){
	
	dirnode_t* node = vfs_newdirnode(name, parent->vnode.fs, NULL, parent);
	
	if(!node) 
		return ENOMEM;

	if(!hashtable_insert(&parent->children, name, node)){
		vfs_destroynode(node);
		return ENOMEM;
	}
	
	stat* st = &node->vnode.st;
	
	st->st_mode = MAKETYPE(TYPE_DIR) | mode;
	st->st_blksize = PAGE_SIZE;

	return 0;
	
}

static int tmpfs_create(dirnode_t* parent, char* name, mode_t mode){
		
	// TODO use virtual memory
	void* firstpage = pmm_hhdmalloc(1);

	if(!firstpage)
		return ENOMEM;

	vnode_t* node = vfs_newnode(name, parent->vnode.fs, NULL);

	if(!node){
		pmm_free(firstpage, 1);
		return ENOMEM;
	}
	
	if(!hashtable_insert(&parent->children, name, node)){
		pmm_free(firstpage, 1);
		vfs_destroynode(node);
		return ENOMEM;
	}
	
	node->parent = parent;
	node->fsdata = firstpage;
	stat* st = &node->st;
	st->st_blksize = PAGE_SIZE;
	st->st_blocks = 1;
	st->st_mode = MAKETYPE(TYPE_REGULAR) | mode;
	st->st_nlink = 1;
	

	return 0;
}

static int tmpfs_getdirent(dirnode_t* node, dent_t* buff, size_t count, uintmax_t offset, size_t* readcount){

	*readcount = 0;

	for(uintmax_t i = 0; i < count; ++i){
		dent_t* d = &buff[i];
		vnode_t* child = hashtable_fromoffset(&node->children, offset, d->d_name);
		if(!child){
			*readcount = i;
			return 0;
		}

		d->d_ino = child->st.st_ino;
		d->d_off = offset;
		d->d_reclen = sizeof(dent_t);
		d->d_type = GETTYPE(child->st.st_mode);
		++offset;
	}
	
	*readcount = count;

	return 0;
}

static fscalls_t funcs = {
	tmpfs_mount, tmpfs_unmount, tmpfs_open, tmpfs_close, tmpfs_mkdir, tmpfs_create, tmpfs_write, tmpfs_read, tmpfs_getdirent
};


fscalls_t* tmpfs_getfuncs(){
	return &funcs;
}
