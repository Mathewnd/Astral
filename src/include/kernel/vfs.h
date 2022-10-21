#ifndef _VFS_H_INCLUDE
#define _VFS_H_INCLUDE

#include <sys/stat.h>
#include <hashtable.h>
#include <errno.h>
#include <dirent.h>
#include <poll.h>
#include <stdbool.h>

struct _vnode_t;
struct _fscalls_t;
struct _dirnode_t;

typedef struct {	
	struct _dirnode_t* rootvnode;
	struct _fscalls_t* calls;
	void* data;
} fs_t;


typedef struct _vnode_t {
	stat st;
	char* name;
	fs_t* fs;
	void* fsdata;
	size_t refcount;
	struct _dirnode_t* parent;
	void* objdata;
} vnode_t;

// folders are an expansion of vnode_t to save memory

typedef struct _dirnode_t{
	vnode_t vnode;
	struct _dirnode_t* mount;
	hashtable children;
} dirnode_t;

// fs specific calls called by the vfs on filesystems

typedef struct _fscalls_t {
	// prepares the filesystem using a filesystem data pointer and device, returning the root node in rootptr
	int (*mount)(dirnode_t* mountpoint, vnode_t* device, int mountflags, void* fsinfo);
	// cleanup of the filesystem
	int	 (*umount)(fs_t* fs);
	//	 opens a file under a dirnode
	//	 args: parent, name
	int	 (*open)(dirnode_t* parent, char* name);
	//	 cleans up a node
	//	 args: the node to clean
	int	 (*close)(vnode_t* node);
	//	 creates a new directory with the right mode
	//	 args: parent, name, mode
	int	 (*mkdir)(dirnode_t* parent, char* name, mode_t mode);
	//	 create a new file
	//	 args: parent, name, mode
	int	 (*create)(dirnode_t* parent, char* name, mode_t mode);
	//	 write to a file
	int	 (*write)(int* error, vnode_t* node, void* buff, size_t count, size_t offset);
	//	 read from a file
	int	 (*read)(int* error, vnode_t* node, void* buff, size_t count, size_t offset);
	//	 get dir ents
	int	 (*getdirent)(dirnode_t* node, dent_t* buff, size_t count, uintmax_t offset, size_t* readcount);
	//	 change file mode
	int 	 (*chmod)(vnode_t* node, mode_t mode);
	//	 create socket in address
	int	 (*mksocket)(dirnode_t* ref, char* path, mode_t mode);
	//	 creates a symlink
	int	 (*symlink)(dirnode_t* ref, char* path, char* target, mode_t mode);
	//	 reads a symlink
	int	 (*readlink)(vnode_t* node, char** buff, size_t* linksize);
} fscalls_t;

int vfs_mksocket(dirnode_t* ref, char* path, mode_t mode);
int vfs_mount(dirnode_t* ref, char* device, char* mountpoint, char* fs, int mountflags, void* fsinfo);
int vfs_umount(dirnode_t* ref, char* mountpoint);
int vfs_open(vnode_t** buf, dirnode_t* ref, char* path);
int vfs_close(vnode_t* node);
int vfs_mkdir(dirnode_t* ref, char* path, mode_t mode);
int vfs_create(dirnode_t* ref, char* path, mode_t mode);
int vfs_write(int* error, vnode_t* node, void* buff, size_t count, size_t offset);
int vfs_read(int* error, vnode_t* node, void* buff, size_t count, size_t offset);
int vfs_isatty(vnode_t* node);
int vfs_getdirent(dirnode_t* node, dent_t* buff, size_t count, uintmax_t offset, size_t* readcount);
int vfs_ioctl(vnode_t* node, unsigned long request, void* arg, int* result);
int vfs_poll(vnode_t* node, pollfd* fd);
int vfs_chmod(vnode_t* node, mode_t mode);
int vfs_symlink(dirnode_t* ref, char* path, char* target, mode_t mode);

void vfs_init();
dirnode_t* vfs_root();
void vfs_acquirenode(vnode_t* node);
void vfs_releasenode(vnode_t* node);
vnode_t* vfs_newnode(char* name, fs_t* fs, void* fsdata);
dirnode_t* vfs_newdirnode(char* name, fs_t* fs, void* fsdata, dirnode_t* parent);
void vfs_destroynode(vnode_t* node);
int  vfs_resolvepath(vnode_t** result, dirnode_t** parent, dirnode_t* ref, char* path, char* name, bool followlinks, int maxloop);

#define UNIMPLEMENTED { return ENOSYS; }

#endif
