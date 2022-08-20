#include <kernel/vfs.h>
#include <kernel/alloc.h>
#include <hashtable.h>
#include <stdio.h>
#include <string.h>
#include <kernel/devman.h>

dirnode_t* vfsroot;
hashtable  fsfuncs;

static inline dirnode_t* mountpoint(dirnode_t* node){
	dirnode_t* ret = node;
	while(ret->mount)
		ret = ret->mount;
	return ret;
}

void vfs_debugdumptree(dirnode_t* node, size_t depth, size_t maxdepth){
	
	if(depth > maxdepth)
		return;
	
	printf("Directory node: %s Children: ", node->vnode.name);

	// print all children first
	
	node = mountpoint(node);

	for(size_t i = 0; i < node->children.size; ++i){
		hashtableentry* entry = &node->children.entries[i];

		while(entry && entry->key){
			printf("%s ", entry->key);
			entry = entry->next;
		}
		
	}
	// now recurse into directories
	
	printf("\n");

	for(size_t i = 0; i < node->children.size; ++i){
		hashtableentry* entry = &node->children.entries[i];
		
		while(entry && entry->val){
			vnode_t* chnode = entry->val;
			
			if(GETTYPE(chnode->st.st_mode) == TYPE_DIR)
				vfs_debugdumptree((dirnode_t*)chnode, depth + 1, maxdepth);
			entry = entry->next;
		}

		
	}


}

int vfs_write(int* error, vnode_t* node, void* buff, size_t count, size_t offset){


	int type = GETTYPE(node->st.st_mode);
	
	if(type == TYPE_DIR || type == TYPE_LINK){
		*error = EINVAL;
		return -1;
	}

	if(type == TYPE_BLOCKDEV || type == TYPE_CHARDEV){
		int writecount = devman_write(error, node->st.st_rdev, buff, count, offset);
		
		return writecount;
	}

	int writecount = node->fs->calls->write(error, node, buff, count, offset);

	return writecount;
	
}

int vfs_isatty(vnode_t* node){
	
	if(GETTYPE(node->st.st_mode) != TYPE_CHARDEV)
		return ENOTTY;
	
	return devman_isatty(node->st.st_rdev);
	
}

int vfs_read(int* error, vnode_t* node, void* buff, size_t count, size_t offset){
	
	int type = GETTYPE(node->st.st_mode);
	
	if(type == TYPE_DIR || type == TYPE_LINK){
		*error = EINVAL;
		return -1;
	}

	if(type == TYPE_BLOCKDEV || type == TYPE_CHARDEV){
		int readcount = devman_read(error, node->st.st_rdev, buff, count, offset);
		return readcount;
	}

	int readcount = node->fs->calls->read(error, node, buff, count, offset);

	return readcount;

}

int vfs_close(vnode_t* node){
	
	vfs_releasenode(node);

	int status = 0;

	if(node->refcount == 0)
		status = node->fs->calls->close(node);
	
	return status;
}

int vfs_open(vnode_t** buff, dirnode_t* ref, char* path){
	
	vnode_t* node;
	
	int res = vfs_resolvepath(&node, NULL, ref, path, NULL);
	
	if(res)
		return res;

	vfs_acquirenode(node);

	*buff = node;

	return 0;
	
}

int vfs_create(dirnode_t* ref, char* path, mode_t mode){
	
	dirnode_t* parent = NULL;
	dirnode_t* buff   = NULL;

	char name[512];

	int result = vfs_resolvepath(&buff, &parent, ref, path, name);

	if(!parent)
		return result;

	if(buff)
		return EEXIST;

	result = parent->vnode.fs->calls->create(parent, name, NULL);
	
	if(result) return result;
	
	return 0;
}

int vfs_mkdir(dirnode_t* ref, char* path, mode_t mode){
	
	dirnode_t* parent = NULL;
	dirnode_t* buff = NULL;

	char name[512];

	int result = vfs_resolvepath(&buff, &parent, ref, path, name);

	if(!parent)
		return result;

	if(buff)
		return EEXIST;

	result = parent->vnode.fs->calls->mkdir(parent, name, NULL);
	
	if(result) return result;
	
	return 0;
}

int vfs_mount(dirnode_t* ref, char* device, char* mountpoint, char* fs, int mountflags, void* fsinfo){
	
	fscalls_t* fscalls = hashtable_get(&fsfuncs, fs);
	
	if(!fscalls)
		return ENODEV;

	vnode_t* dev = NULL;
	dirnode_t* mountdir = ref;
	dirnode_t* parent = NULL;

	if(device){
		dev = ref;
		if(*device){
			int result = vfs_resolvepath(&dev, &parent, ref, device, NULL);
			
			if(result)
				return result;
		}
	}


	int result = vfs_resolvepath(&mountdir, &parent, ref, mountpoint, NULL);
	
	if(result)
		return result;
	
	if(GETTYPE(mountdir->vnode.st.st_mode) != TYPE_DIR)
		return ENOTDIR;

	result = fscalls->mount(mountdir, dev, mountflags, fsinfo);

	return result;
}

vnode_t* vfs_newnode(char* name, fs_t* fs, void* fsdata){
	vnode_t* node = alloc(sizeof(vnode_t));
	if(!node) return NULL;
	node->name = alloc(strlen(name)+1);
	if(!name){
		free(node);
		return NULL;
	}

	node->fs = fs;
	node->fsdata = fsdata;
	strcpy(node->name, name);

	return node;
}


dirnode_t* vfs_newdirnode(char* name, fs_t* fs, void* fsdata){
	
	dirnode_t* node = alloc(sizeof(dirnode_t));
	if(!node) return NULL;
	vnode_t* vnode = (vnode_t*)node;
	
	vnode->name = alloc(strlen(name)+1);

	if(!vnode->name){
		free(node);
		return NULL;
	}

	if(!hashtable_init(&node->children, 10)){ // add more here later
		free(vnode->name);
		free(node);
		return NULL;
	}
	
	vnode->st.st_mode = MAKETYPE(TYPE_DIR);
	vnode->fs = fs;
	vnode->fsdata = fsdata;
	strcpy(vnode->name, name);

	return node;

}

int vfs_resolvepath(vnode_t** result, dirnode_t** resultparent, dirnode_t* ref, char *path, char* namebuff){
	
	dirnode_t* iterator = ref;
	char name[512];
	off_t nameoffset = 0;

	memset(name, 0, 512);
	
	iterator = mountpoint(iterator);

	while(nameoffset < strlen(path)){
		
		size_t offset = 0;

		while(path[nameoffset] == '/')
			++nameoffset;

		while(path[nameoffset] && path[nameoffset] != '/')
			name[offset++] = path[nameoffset++];
	
		while(path[nameoffset] == '/')
			++nameoffset;

		name[offset] = 0;

		// make sure we're a directory

		if(!GETTYPE(iterator->vnode.st.st_mode) == TYPE_DIR)
			return ENOTDIR;

		// get the right vnode for this point
		
		iterator = mountpoint(iterator);

		// now try to get the right child
		
		vnode_t* child = hashtable_get(&iterator->children, name);

		if(!child){
			// open it
			int status = iterator->vnode.fs->calls->open(iterator, name);
			if(status){
				if(!path[nameoffset]){
					if(namebuff)
						strcpy(namebuff, name);
					if(resultparent)
						*resultparent = iterator;
				}
				return status;
			}

			child = hashtable_get(&iterator->children, name);
			
		}

		iterator = (dirnode_t*)child;

	}
	
	if(namebuff)
		strcpy(namebuff, name);

	if(resultparent)
		*resultparent = iterator->vnode.parent;
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
	++node->refcount;
}

void vfs_releasenode(vnode_t* node){
	--node->refcount;
}

#include <kernel/tmpfs.h>
#include <kernel/devfs.h>

void vfs_init(){
	printf("Creating a fake root for the VFS\n");
	
	vfsroot = vfs_newdirnode("/", NULL, NULL);

	hashtable_init(&fsfuncs, 10);

	hashtable_insert(&fsfuncs, "tmpfs", (void*)tmpfs_getfuncs());
	hashtable_insert(&fsfuncs, "devfs", (void*)devfs_getfuncs());
	
}
