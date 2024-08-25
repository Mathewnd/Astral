#ifndef _AUTH_H
#define _AUTH_H

#include <kernel/cred.h>
#include <kernel/vfs.h>

#define AUTH_SCOPE_COUNT 2
#define AUTH_SCOPE_SYSTEM 0
#define AUTH_SCOPE_FILESYSTEM 1

#define AUTH_LISTENER_LIMIT 8

#define AUTH_DECISION_ALLOW 1
#define AUTH_DECISION_DEFER 0
#define AUTH_DECISION_DENY -1000

#define AUTH_ACTIONS_SYSTEM_CHROOT	1
#define AUTH_ACTIONS_SYSTEM_SETHOSTNAME 2

#define AUTH_ACTIONS_FILESYSTEM_EXECUTE 1
#define AUTH_ACTIONS_FILESYSTEM_WRITE 	2
#define AUTH_ACTIONS_FILESYSTEM_READ 	4
#define AUTH_ACTIONS_FILESYSTEM_SETATTR 8

typedef int (*authlistener_t)(cred_t *cred, int actions, void *arg0, void *arg1, void *arg2);

typedef struct {
	authlistener_t listeners[AUTH_LISTENER_LIMIT];
} authscope_t;

void auth_registerlistener(int scope, authlistener_t listener);
int auth_check(int scope, cred_t *cred, int actions, void *arg0, void *arg1, void *arg2);

static inline int auth_filesystem_check(cred_t *cred, int actions, vnode_t *vnode) {
	return auth_check(AUTH_SCOPE_FILESYSTEM, cred, actions, vnode, NULL, NULL);
}

static inline int auth_system_check(cred_t *cred, int actions) {
	return auth_check(AUTH_SCOPE_SYSTEM, cred, actions, NULL, NULL, NULL);
}

static inline int auth_filesystem_convertaccess(int access) {
	int r = 0;

	if (access & V_ACCESS_EXECUTE)
		r |= AUTH_ACTIONS_FILESYSTEM_EXECUTE;
	if (access & V_ACCESS_WRITE)
		r |= AUTH_ACTIONS_FILESYSTEM_WRITE;
	if (access & V_ACCESS_READ)
		r |= AUTH_ACTIONS_FILESYSTEM_READ;

	return r;
}

#endif
