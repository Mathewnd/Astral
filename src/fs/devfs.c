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

static int devfs_getdirent(dirnode_t* node, dent_t* buff, size_t count, uintmax_t offset, size_t* readcount){

        *readcount = 0;

	// all getdirents on devfs will return the same

        for(uintmax_t i = 0; i < count; ++i){
                dent_t* d = &buff[i];
                vnode_t* child = hashtable_fromoffset(&devnodes, offset, d->d_name);
                
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

static int devfs_chmod(vnode_t* node, mode_t mode){
	node->st.st_mode &= ~07777;
	node->st.st_mode |= mode & 07777;
	return 0;
}

static fscalls_t devfscalls = {
	devfs_mount, devfs_unmount, devfs_open, devfs_close, NULL, NULL, NULL, NULL, devfs_getdirent, devfs_chmod
};

void devfs_init(){

	fs = alloc(sizeof(fs_t));
	fs->calls = &devfscalls;
	
	hashtable_init(&devnodes, 10);

}

int devfs_newdevice(char* name, int type, dev_t dev, mode_t mode){
	
	if(hashtable_get(&devnodes, name))
		return EEXIST;

	vnode_t* node = vfs_newnode(name, fs, 0);
	if(!node)
		return ENOMEM;
	
	if(!hashtable_insert(&devnodes, name, node)){
		vfs_destroynode(node);
		return ENOMEM;
	}

	node->st.st_rdev = dev;
	node->st.st_mode = MAKETYPE(type) | mode;
	node->st.st_ino  = fs->data;

	return 0;
	
}

fscalls_t* devfs_getfuncs(){
	return &devfscalls;
}
