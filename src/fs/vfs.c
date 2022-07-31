#include <kernel/vfs.h>
#include <kernel/alloc.h>
#include <hashtable.h>
#include <stdio.h>
#include <string.h>

dirnode_t* vfsroot;

vnode_t* vfs_newnode(fs_t* fs, void* fsdata){
	vnode_t* node = alloc(sizeof(vnode_t));
	if(!node) return NULL;
	node->fs = fs;
	node->fsdata = fsdata;
	
	return node;
}


dirnode_t* vfs_newdirnode(fs_t* fs, void* fsdata){
	
	dirnode_t* node = alloc(sizeof(dirnode_t));
	if(!node) return NULL;
	vnode_t* vnode = (vnode_t*)node;

	if(!hashtable_init(&node->children, 10)){ // add more here later
		free(node);
		return NULL;
	}
	
	vnode->st.st_mode = MAKETYPE(TYPE_DIR);
	vnode->fs = fs;
	vnode->fsdata = fsdata;
}

int vfs_resolvepath(vnode_t** result, dirnode_t* ref, char *path){
	
	dirnode_t* iterator = ref;
	char name[512];
	off_t nameoffset = 0;

	while(nameoffset <= strlen(path)){
		
		size_t offset = 0;

		while(path[nameoffset])
			name[offset++] = path[nameoffset++];

		name[offset] = 0;

		// make sure we're a directory

		if(!GETTYPE(iterator->vnode.st.st_mode) == TYPE_DIR)
			return ENOTDIR;
		
		// find correct node if mountmoint

		while(iterator->mountednode)
			iterator = iterator->mountednode;
		
		vnode_t* child = hashtable_get(&iterator->children, name);

		if(!child){
			// open it
			int status = iterator->vnode.fs->calls->open(iterator, name);
			if(status)
				return status;

			child = hashtable_get(&iterator->children, name);
			
		}

		iterator = (dirnode_t*)child;

	}
	
	*result = (vnode_t*)iterator;

	return 0;

}

void vfs_destroynode(vnode_t* node){
	
	if(GETTYPE(node->st.st_mode) == TYPE_DIR)
		hashtable_destroy(&((dirnode_t*)node)->children);
	
	free(node);

}

dirnode_t* vfs_root(){
	return vfsroot;
}

void vfs_acquirenode(vnode_t* node){
	while(node){
		++node->refcount;
		node = (vnode_t*)node->parent;
	}
}

void vfs_releasenode(vnode_t* node){
	while(node){
		--node->refcount;
		node = (vnode_t*)node->parent;
	}
}

void vfs_init(){
	printf("Creating a fake root for the VFS\n");
	
	vfsroot = vfs_newdirnode(NULL, NULL);
}
