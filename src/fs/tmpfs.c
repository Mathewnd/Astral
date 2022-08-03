#include <kernel/tmpfs.h>
#include <kernel/vfs.h>
#include <kernel/alloc.h>

static int tmpfs_mount(dirnode_t** rootptr, vnode_t* device, int mountflags, void* fsinfo){
	
	fs_t* desc = alloc(sizeof(fs_t));
	
	if(!desc)
		return ENOMEM;

	dirnode_t* root = vfs_newdirnode(desc, NULL);

	if(!root){
		free(desc);
		return ENOMEM;
	}
	
	*rootptr = root;
	
	return 0;
}

static fscalls_t funcs = {
	tmpfs_mount
};


fscalls_t* tmpfs_getfuncs(){
	return &funcs;
}
