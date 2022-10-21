#include <kernel/vfs.h>
#include <kernel/alloc.h>
#include <hashtable.h>
#include <stdio.h>
#include <string.h>
#include <kernel/devman.h>
#include <arch/spinlock.h>
#include <dirent.h>
#include <kernel/pipe.h>
#include <arch/timekeeper.h>

#define VFS_MAX_LOOP 5

dirnode_t* vfsroot;
hashtable  fsfuncs;
static int lock;

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
	
	if(type == TYPE_SOCKET)
		return socket_send(node->objdata, buff, count, 0, error);

	if(type == TYPE_FIFO)
		return pipe_write(node->objdata, buff, count, error);
	

	if(type == TYPE_DIR || type == TYPE_LINK){
		*error = EINVAL;
		return -1;
	}

	if(type == TYPE_BLOCKDEV || type == TYPE_CHARDEV){
		int writecount = devman_write(error, node->st.st_rdev, buff, count, offset);
		
		return writecount;
	}

	
	spinlock_acquire(&lock);
	
	int writecount = node->fs->calls->write(error, node, buff, count, offset);
	
	spinlock_release(&lock);

	return writecount;
	
}

int vfs_isatty(vnode_t* node){
	
	if(GETTYPE(node->st.st_mode) != TYPE_CHARDEV)
		return ENOTTY;
	
	return devman_isatty(node->st.st_rdev);
	
}

int vfs_chmod(vnode_t* node, mode_t mode){
	
	if(!node->fs->calls->chmod)
		return ENOSYS;

	spinlock_acquire(&lock);

	int ret = node->fs->calls->chmod(node, mode);

	spinlock_release(&lock);

	return ret;

}

int vfs_poll(vnode_t* node, pollfd* fd){
	
	switch(GETTYPE(node->st.st_mode)){
		case TYPE_CHARDEV:
		case TYPE_BLOCKDEV:
			return devman_poll(node->st.st_rdev, fd);
		case TYPE_FIFO:
			return pipe_poll(node->objdata, fd);
		default:
			_panic("Unsupported poll", NULL);	
	}

}

int vfs_ioctl(vnode_t* node, unsigned long request, void* arg, int* result){
	
	if(GETTYPE(node->st.st_mode) != TYPE_CHARDEV)
		return ENOTTY;
	
	return devman_ioctl(node->st.st_rdev, request, arg, result);
	
}

int vfs_read(int* error, vnode_t* node, void* buff, size_t count, size_t offset){
	
	int type = GETTYPE(node->st.st_mode);
	
	if(type == TYPE_SOCKET)
		return socket_recv(node->objdata, buff, count, 0, error);

	if(type == TYPE_FIFO){
		return pipe_read(node->objdata, buff, count, error);
	}

	if(type == TYPE_DIR || type == TYPE_LINK){
		*error = EINVAL;
		return -1;
	}

	if(type == TYPE_BLOCKDEV || type == TYPE_CHARDEV){
		int readcount = devman_read(error, node->st.st_rdev, buff, count, offset);
		return readcount;
	}
	
	spinlock_acquire(&lock);
	
	int readcount = node->fs->calls->read(error, node, buff, count, offset);
	
	spinlock_release(&lock);

	return readcount;

}

int vfs_close(vnode_t* node){
		
	spinlock_acquire(&lock);
	
	vfs_releasenode(node);

	int status = 0;
	
	// TODO free pipe if one

	if(node->refcount == 0)
		status = node->fs->calls->close(node);
	
	spinlock_release(&lock);
	
	return status;
}

int vfs_getdirent(dirnode_t* node, dent_t* buff, size_t count, uintmax_t offset, size_t* readcount){
	
	int err = 0;

	spinlock_acquire(&lock);
	

	if(GETTYPE(node->vnode.st.st_mode) != TYPE_DIR){
		err = ENOTDIR;
		goto _ret;
	}

	err = node->vnode.fs->calls->getdirent(node, buff, count, offset, readcount);

	_ret:

	spinlock_release(&lock);

	return err;
}


int vfs_open(vnode_t** buff, dirnode_t* ref, char* path){

	vnode_t* node;
	
	spinlock_acquire(&lock);
	
	int res = vfs_resolvepath(&node, NULL, ref, path, NULL, true, VFS_MAX_LOOP);
	
	if(res){
		spinlock_release(&lock);
		return res;
	}
	vfs_acquirenode(node);
	
	spinlock_release(&lock);

	*buff = node;

	return 0;
	
}

int vfs_create(dirnode_t* ref, char* path, mode_t mode){
	
	dirnode_t* parent = NULL;
	dirnode_t* buff   = NULL;

	char name[512];
	
	spinlock_acquire(&lock);

	int result = vfs_resolvepath(&buff, &parent, ref, path, name, true, VFS_MAX_LOOP);

	if(buff){
		spinlock_release(&lock);
		return EEXIST;
	}

	if(!parent){
		spinlock_release(&lock);
		return result;
	}

	result = parent->vnode.fs->calls->create(parent, name, mode);
	
	spinlock_release(&lock);
	
	if(result) return result;
	
	return 0;
}

int vfs_mksocket(dirnode_t* ref, char* path, mode_t mode){
		
	dirnode_t* parent = NULL;
	dirnode_t* buff   = NULL;

	char name[512];
	
	spinlock_acquire(&lock);

	int result = vfs_resolvepath(&buff, &parent, ref, path, name, true, VFS_MAX_LOOP);

	if(buff){
		spinlock_release(&lock);
		return EEXIST;
	}

	if(!parent){
		spinlock_release(&lock);
		return result;
	}

	if(!parent->vnode.fs->calls->mksocket){
		spinlock_release(&lock);
		return ENOSYS;
	}

	result = parent->vnode.fs->calls->mksocket(parent, name, mode);
	
	spinlock_release(&lock);
	
	return result;
}

int vfs_symlink(dirnode_t* ref, char* path, char* target, mode_t mode){
	
	if(strlen(target) >= 512)
		return ENAMETOOLONG;
	
	dirnode_t* parent = NULL;
	dirnode_t* buff   = NULL;
	char name[512];

        spinlock_acquire(&lock);

        int result = vfs_resolvepath(&buff, &parent, ref, path, name, true, VFS_MAX_LOOP);

        if(buff){
                spinlock_release(&lock);
                return EEXIST;
        }

        if(!parent){
                spinlock_release(&lock);
                return result;
        }

        if(!parent->vnode.fs->calls->symlink){
                spinlock_release(&lock);
                return ENOSYS;
        }

        result = parent->vnode.fs->calls->symlink(parent, name, target, mode);

        spinlock_release(&lock);

	return result;


}

int vfs_link(dirnode_t* ref, vnode_t* link, char* path){
	
	dirnode_t* parent = NULL;
	dirnode_t* buff   = NULL;
	char name[512];

        spinlock_acquire(&lock);

        int result = vfs_resolvepath(&buff, &parent, ref, path, name, true, VFS_MAX_LOOP);

        if(buff){
                spinlock_release(&lock);
                return EEXIST;
        }

        if(!parent){
                spinlock_release(&lock);
                return result;
        }

        if(!parent->vnode.fs->calls->link){
                spinlock_release(&lock);
                return ENOSYS;
        }
	
	if(link->fs != parent->vnode.fs)
		return EXDEV;

        result = parent->vnode.fs->calls->link(parent, link, name);

        spinlock_release(&lock);

	return result;


}


int vfs_mkdir(dirnode_t* ref, char* path, mode_t mode){

	dirnode_t* parent = NULL;
	dirnode_t* buff = NULL;

	char name[512];
	
	spinlock_acquire(&lock);

	int result = vfs_resolvepath(&buff, &parent, ref, path, name, true, VFS_MAX_LOOP);

	if(buff){
		spinlock_release(&lock);
		return EEXIST;
	}

	if(!parent){
		spinlock_release(&lock);
		return result;
	}

	result = parent->vnode.fs->calls->mkdir(parent, name, mode);
	
	spinlock_release(&lock);
	
	return result;
}

int vfs_mount(dirnode_t* ref, char* device, char* mountpoint, char* fs, int mountflags, void* fsinfo){
	
	fscalls_t* fscalls = hashtable_get(&fsfuncs, fs);
	
	if(!fscalls)
		return ENODEV;

	vnode_t* dev = NULL;
	dirnode_t* mountdir = ref;
	dirnode_t* parent = NULL;
	
	spinlock_acquire(&lock);

	if(device){
		dev = ref;
		if(*device){
			int result = vfs_resolvepath(&dev, &parent, ref, device, NULL, true, VFS_MAX_LOOP);
			
			if(result){
				spinlock_release(&lock);
				return result;
			}
		}
	}


	int result = vfs_resolvepath(&mountdir, &parent, ref, mountpoint, NULL, true, VFS_MAX_LOOP);
	
	if(result){
		spinlock_release(&lock);
		return result;
	}

	if(GETTYPE(mountdir->vnode.st.st_mode) != TYPE_DIR){
		spinlock_release(&lock);
		return ENOTDIR;
	}

	result = fscalls->mount(mountdir, dev, mountflags, fsinfo);
	
	spinlock_release(&lock);

	return result;
}

#define ADDTIMETOSTAT(st) st.st_atim = st.st_ctim = st.st_mtim = arch_timekeeper_gettime()

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

	ADDTIMETOSTAT(node->st);

	return node;
}


dirnode_t* vfs_newdirnode(char* name, fs_t* fs, void* fsdata, dirnode_t* parent){
	
	dirnode_t* node = alloc(sizeof(dirnode_t));
	if(!node) return NULL;
	vnode_t* vnode = (vnode_t*)node;
	
	vnode->name = alloc(strlen(name)+1);

	if(!vnode->name){
		free(node);
		return NULL;
	}

	if(!hashtable_init(&node->children, 25)){ // add more here later
		free(vnode->name);
		free(node);
		return NULL;
	}

	hashtable_insert(&node->children, ".", node);
	hashtable_insert(&node->children, "..", parent);

	vnode->st.st_mode = MAKETYPE(TYPE_DIR);
	vnode->fs = fs;
	vnode->fsdata = fsdata;
	strcpy(vnode->name, name);

	ADDTIMETOSTAT(vnode->st);

	return node;

}

// TODO the stack usage is scarily high

int vfs_resolvepath(vnode_t** result, dirnode_t** resultparent, dirnode_t* ref, char *path, char* namebuff, bool followlinks, int maxloop){
	
	if(strlen(path) >= 512)
		return ENAMETOOLONG;

	if(maxloop == 0)
		return ELOOP;

	dirnode_t* iterator = ref;
	char name[512];
	off_t nameoffset = 0;

	memset(name, 0, 256);
	
	iterator = mountpoint(iterator);

	while(nameoffset < strlen(path)){
		
		size_t offset = 0;

		while(path[nameoffset] == '/')
			++nameoffset;

		while(path[nameoffset] && path[nameoffset] != '/')
			name[offset++] = path[nameoffset++];
	
		// if there are only separators in the filename

		if(!offset) break;

		while(path[nameoffset] == '/')
			++nameoffset;

		name[offset] = 0;
		
		// do we need to follow a symlink

		if(GETTYPE(iterator->vnode.st.st_mode) == TYPE_LINK && followlinks){
				
			if(!iterator->vnode.fs->calls->readlink)
				return ENOSYS;

			size_t size;
			char* linkname;
			
			int error = iterator->vnode.fs->calls->readlink(iterator, &linkname, &size);
			if(error)
				return error;
			
			linkname[size] = '\0';
			
			printf("reading link: %s\n", linkname);

			error = vfs_resolvepath(&iterator, NULL, iterator, linkname, namebuff, true, maxloop-1); // XXX this might break some parent stuff when following links

			free(linkname);

			if(error)
				return error;
		
		}

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
	
	if(GETTYPE(iterator->vnode.st.st_mode) == TYPE_DIR)
		iterator = mountpoint(iterator);


	// check if result is a symlink

	if(GETTYPE(iterator->vnode.st.st_mode) == TYPE_LINK && followlinks){
		if(!iterator->vnode.fs->calls->readlink)
			return ENOSYS;

		size_t size;
		char* linkname;
		
		int error = iterator->vnode.fs->calls->readlink(iterator, &linkname, &size);
		if(error)
			return error;
		
		linkname[size] = '\0';

		error = vfs_resolvepath(&iterator, NULL, iterator->vnode.parent, linkname, namebuff, true, maxloop-1); // XXX this might break some parent stuff when following links

		free(linkname);

		if(error)
			return error;
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
	++node->refcount;
}

void vfs_releasenode(vnode_t* node){
	--node->refcount;
}

#include <kernel/tmpfs.h>
#include <kernel/devfs.h>

void vfs_init(){
	printf("Creating a fake root for the VFS\n");
	
	vfsroot = vfs_newdirnode("/", NULL, NULL, NULL);

	hashtable_init(&fsfuncs, 10);

	hashtable_insert(&fsfuncs, "tmpfs", (void*)tmpfs_getfuncs());
	hashtable_insert(&fsfuncs, "devfs", (void*)devfs_getfuncs());
	
}
