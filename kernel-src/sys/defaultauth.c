#include <kernel/auth.h>
#include <logging.h>

// default listeners for auth

#define DONE_CHECK(a) {\
		if ((a) == 0) \
			goto done; \
	}

static int filesystem(cred_t *cred, int actions, void *arg0, void *arg1, void *arg2) {
	vnode_t *vnode = arg0;

	vattr_t attr;
	VOP_GETATTR(vnode, &attr, NULL);

	int weight = 0;
	if (actions & AUTH_ACTIONS_FILESYSTEM_WRITE) {
		if (CRED_IS_ESU(cred))
			weight += 1;
		else if (attr.uid == cred->euid && (attr.mode & V_ATTR_MODE_USER_WRITE))
			weight += 1;
		else if (attr.uid != cred->euid && attr.gid == cred->egid && (attr.mode & V_ATTR_MODE_GROUP_WRITE))
			weight += 1;
		else if (attr.uid != cred->euid && attr.gid != cred->egid && (attr.mode & V_ATTR_MODE_OTHERS_WRITE))
			weight += 1;
		else
			return AUTH_DECISION_DENY;

		DONE_CHECK(actions);
	}

	if (actions & AUTH_ACTIONS_FILESYSTEM_READ) {
		if (CRED_IS_ESU(cred))
			weight += 1;
		else if (attr.uid == cred->euid && (attr.mode & V_ATTR_MODE_USER_READ))
			weight += 1;
		else if (attr.uid != cred->euid && attr.gid == cred->egid && (attr.mode & V_ATTR_MODE_GROUP_READ))
			weight += 1;
		else if (attr.uid != cred->euid && attr.gid != cred->egid && (attr.mode & V_ATTR_MODE_OTHERS_READ))
			weight += 1;
		else
			return AUTH_DECISION_DENY;

		DONE_CHECK(actions);
	}

	if (actions & AUTH_ACTIONS_FILESYSTEM_EXECUTE) {
		if (vnode->type == V_TYPE_DIR && CRED_IS_ESU(cred))
			weight += 1;
		else if ((attr.mode & (V_ATTR_MODE_USER_EXECUTE | V_ATTR_MODE_GROUP_EXECUTE | V_ATTR_MODE_OTHERS_EXECUTE)) && CRED_IS_ESU(cred))
			weight += 1;
		else if (attr.uid == cred->euid && (attr.mode & V_ATTR_MODE_USER_EXECUTE))
			weight += 1;
		else if (attr.uid != cred->euid && attr.gid == cred->egid && (attr.mode & V_ATTR_MODE_GROUP_EXECUTE))
			weight += 1;
		else if (attr.uid != cred->euid && attr.gid != cred->egid && (attr.mode & V_ATTR_MODE_OTHERS_EXECUTE))
			weight += 1;
		else
			return AUTH_DECISION_DENY;

		DONE_CHECK(actions);
	}

	if (actions & AUTH_ACTIONS_FILESYSTEM_SETATTR) {
		if (CRED_IS_ESU(cred) || cred->euid == attr.uid)
			weight += 1;
		else
			return AUTH_DECISION_DENY;

		DONE_CHECK(actions);
	}

	done:
	return weight ? AUTH_DECISION_ALLOW : AUTH_DECISION_DEFER;
}

static int system(cred_t *cred, int actions, void *arg0, void *arg1, void *arg2) {
	int weight = 0;

	if (actions & AUTH_ACTIONS_SYSTEM_CHROOT) {
		if (CRED_IS_ESU(cred))
			weight += 1;
		else
			return AUTH_DECISION_DENY;

		DONE_CHECK(actions);
	}


	done:
	return weight ? AUTH_DECISION_ALLOW : AUTH_DECISION_DEFER;
}

void defaultauth_init() {
	auth_registerlistener(AUTH_SCOPE_FILESYSTEM, filesystem);
	auth_registerlistener(AUTH_SCOPE_SYSTEM, system);
}
