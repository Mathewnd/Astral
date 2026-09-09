#include <kernel/firmware.h>
#include <kernel/vfs.h>
#include <kernel/alloc.h>
#include <errno.h>

#define FIRMWARE_SIZE_LIMIT (16 * 1024)
#define FIRMWARE_PATH "/usr/lib/firmware/"

typedef struct {
	const char *name;
	firmware_callback_t callback;
	void *ctx;
} firmware_ctx_t;

static void do_load(void *ctx) {
	firmware_ctx_t *fc = ctx;
	printf("firmware: loading \"%s\"\n", fc->name);

	vnode_t *firmware_dir;
	int error = vfs_lookup(&firmware_dir, vfsroot, FIRMWARE_PATH, NULL, 0);
	if (error) {
		printf("firmware: loading \"%s\" failed: failed to open %s (%s)\n",
			       	fc->name, FIRMWARE_PATH, strerror(error));
		goto leave;
	}

	VOP_UNLOCK(firmware_dir); // vfs_lookup returns vnode locked

	vnode_t *firmware_file;
	error = vfs_lookup(&firmware_file, firmware_dir, fc->name, NULL, 0);
	VOP_RELEASE(firmware_dir);
	if (error) {
		printf("firmware: loading \"%s\" failed: failed to open file (%s)\n", 
				fc->name, strerror(error));
		goto leave;
	}

	vattr_t vattr;
	error = VOP_GETATTR(firmware_file, &vattr, NULL);
	VOP_UNLOCK(firmware_file);
	if (error)
		goto leave_release;

	if (vattr.size >= FIRMWARE_SIZE_LIMIT) {
		error = EINVAL;
		goto leave_release;
	}

	void *buffer = alloc(vattr.size);
	if (buffer == NULL) {
		error = ENOMEM;
		goto leave_release;
	}

	size_t bytes_read;
	error = vfs_read(firmware_file, buffer, vattr.size, 0, &bytes_read, 0); 
	if (error)
		goto leave_free;

	if (bytes_read != vattr.size) {
		error = EINVAL;
		goto leave_free;
	}

	printf("firmware: \"%s\" loaded\n", fc->name);
	fc->callback(fc->ctx, buffer, vattr.size);

leave_free:
	free(buffer);
leave_release:
	if (error) {
		printf("firmware: loading \"%s\" failed: %s\n", 
				fc->name, strerror(error));
	}
	VOP_RELEASE(firmware_file);
leave:
	free(fc);
}

int firmware_load(const char *name, firmware_callback_t callback, void *ctx) {
	// TODO: support kernel embedded firmware
	firmware_ctx_t *fc = alloc(sizeof(firmware_ctx_t));
	if (fc == NULL)
		return ENOMEM;

	fc->name = name;
	fc->callback = callback;
	fc->ctx = ctx;

	int error = vfs_root_event_attach(do_load, fc);
	if (error && error != EBUSY) {
		free(fc);
		return error;
	} else if (error == EBUSY) {
		do_load(fc);
	}

	return 0;
}
