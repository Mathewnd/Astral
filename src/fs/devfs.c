#include <kernel/devfs.h>
#include <kernel/vfs.h>
#include <kernel/alloc.h>
#include <hashtable.h>

#include <sys/sysmacros.h>

static hashtable devnodes;
static fs_t* fs;

static int devfs_mount(dirnode_t* mountpoint, vnode_t* device, int mountflags, void* fsinfo){
	
	dirnode_t* node = vfs_newdirnode(mountpoint->vnode.name, fs, NULL, mountpoint->vnode.parent);
	
	if(!node)
		return ENOMEM;
	
	mountpoint->mount = node;
	
	return 0;
}

static int devfs_unmount() UNIMPLEMENTED
static int devfs_open(dirnode_t* parent, char* name){
	
	vnode_t* devnode = hashtable_get(&devnodes, name);
	
	if(!devnode)
		return ENOENT;

	vnode_t* node = vfs_newnode(name, fs, devnode);
	
	if(!node)
		return ENOMEM;

	if(!hashtable_insert(&parent->children, name, node)){
		vfs_destroynode(node);
		return ENOMEM;
	}

	node->st = devnode->st;

	return 0;

}

static int devfs_close(){return 0;}
static int devfs_mkdir() UNIMPLEMENTED
static int devfs_create() UNIMPLEMENTED
static int devfs_write() UNIMPLEMENTED
static int devfs_read() UNIMPLEMENTED


static fscalls_t devfscalls = {
	devfs_mount, devfs_unmount, devfs_open, devfs_close, devfs_mkdir, devfs_create, devfs_write, devfs_read
};

void devfs_init(){

	fs = alloc(sizeof(fs_t));
	fs->calls = &devfscalls;
	
	hashtable_init(&devnodes, 10);

}



int devfs_newdevice(char* name, int type, dev_t dev, mode_t mode){
	
	vnode_t* node = vfs_newnode(name, fs, 0);
	if(!node)
		return ENOMEM;
	
	if(!hashtable_insert(&devnodes, name, node)){
		vfs_destroynode(node);
		return ENOMEM;
	}
	
	node->st.st_rdev = dev;
	node->st.st_mode = MAKETYPE(type) | mode;

	return 0;
	
}

fscalls_t* devfs_getfuncs(){
	return &devfscalls;
}
