#include <kernel/tmpfs.h>
#include <kernel/vfs.h>
#include <kernel/alloc.h>
#include <arch/mmu.h>

static int tmpfs_mount(dirnode_t* mountpoint, vnode_t* device, int mountflags, void* fsinfo){
	
	fs_t* desc = alloc(sizeof(fs_t));
	
	if(!desc)
		return ENOMEM;

	dirnode_t* root = vfs_newdirnode(mountpoint->vnode.name, desc, NULL);

	if(!root){
		free(desc);
		return ENOMEM;
	}
	
	desc->calls = tmpfs_getfuncs();
	mountpoint->mount = root;

	return 0;
}

static int tmpfs_unmount(fs_t* fs) UNIMPLEMENTED
static int tmpfs_open(dirnode_t* parent, char* name) UNIMPLEMENTED
static int tmpfs_close(vnode_t* node) UNIMPLEMENTED

static int tmpfs_mkdir(dirnode_t* parent, char* name, mode_t mode){
	
	dirnode_t* node = vfs_newdirnode(name, parent->vnode.fs, NULL);
	
	if(!node) 
		return ENOMEM;

	if(!hashtable_insert(&parent->children, name, node)){
		vfs_destroynode(node);
		return ENOMEM;
	}
	
	node->vnode.parent = parent;	
	stat* st = &node->vnode.st;
	st->st_mode = MAKETYPE(TYPE_DIR) | GETMODE(mode);
	st->st_blksize = PAGE_SIZE;

	return 0;
	
}

static int tmpfs_create(dirnode_t* parent, char* name, mode_t mode){
	
	vnode_t* node = vfs_newnode(name, parent->vnode.fs, NULL);
	
	if(!node)
		return ENOMEM;
	
	if(!hashtable_insert(&parent->children, name, node)){
		vfs_destroynode(node);
		return ENOMEM;
	}
	
	node->parent = parent;
	stat* st = &node->st;
	st->st_mode = MAKETYPE(TYPE_DIR) | GETMODE(mode);
	st->st_blksize = PAGE_SIZE;

	return 0;
}

static fscalls_t funcs = {
	tmpfs_mount, tmpfs_unmount, tmpfs_open, tmpfs_close, tmpfs_mkdir, tmpfs_create
};


fscalls_t* tmpfs_getfuncs(){
	return &funcs;
}
