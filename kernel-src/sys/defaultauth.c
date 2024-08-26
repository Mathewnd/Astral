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

	if (actions & AUTH_ACTIONS_FILESYSTEM_MOUNT) {
		// vnode is where it will be mounted on
		if (CRED_IS_ESU(cred))
			weight += 1;
		else
			return AUTH_DECISION_DENY;
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

	if (actions & AUTH_ACTIONS_SYSTEM_SETHOSTNAME) {
		if (CRED_IS_ESU(cred))
			weight += 1;
		else
			return AUTH_DECISION_DENY;

		DONE_CHECK(actions);
	}

	done:
	return weight ? AUTH_DECISION_ALLOW : AUTH_DECISION_DEFER;
}

static int credlistener(cred_t *cred, int actions, void *arg0, void *arg1, void *arg2) {
	int *id = arg0;
	int *eid = arg1;
	int *sid = arg2;

	int weight = 0;
	if (actions & AUTH_ACTIONS_CRED_SETRESUID) {
		if (CRED_IS_ESU(cred)) {
			weight += 1;
		} else {
			if (*id != -1 && *id != cred->uid && *id != cred->euid && *id != cred->suid)
				return AUTH_DECISION_DENY;
			if (*eid != -1 && *id != cred->uid && *id != cred->euid && *id != cred->suid)
				return AUTH_DECISION_DENY;
			if (*sid != -1 && *sid != cred->uid && *sid != cred->euid && *sid != cred->suid)
				return AUTH_DECISION_DENY;

			weight += 1;
		}

		DONE_CHECK(actions);
	}

	if (actions & AUTH_ACTIONS_CRED_SETRESGID) {
		if (CRED_IS_ESU(cred)) {
			weight += 1;
		} else {
			if (*id != -1 && *id != cred->gid && *id != cred->egid && *id != cred->sgid)
				return AUTH_DECISION_DENY;
			if (*eid != -1 && *id != cred->gid && *id != cred->egid && *id != cred->sgid)
				return AUTH_DECISION_DENY;
			if (*sid != -1 && *sid != cred->gid && *sid != cred->egid && *sid != cred->sgid)
				return AUTH_DECISION_DENY;

			weight += 1;
		}

		DONE_CHECK(actions);
	}

	if (actions & AUTH_ACTIONS_CRED_SETUID) {
		if (CRED_IS_ESU(cred)) {
			weight += 1;
		} else if (*id != cred->uid && *id != cred->suid) {
				return AUTH_DECISION_DENY;
		}

		weight += 1;
		DONE_CHECK(actions)
	}

	if (actions & AUTH_ACTIONS_CRED_SETGID) {
		if (CRED_IS_ESU(cred)) {
			weight += 1;
		} else if (*id != cred->gid && *id != cred->sgid) {
				return AUTH_DECISION_DENY;
		}

		weight += 1;
		DONE_CHECK(actions)
	}

	if (actions & AUTH_ACTIONS_CRED_SETEUID) {
		if (CRED_IS_ESU(cred)) {
			weight += 1;
		} else if (*eid != cred->uid && *eid != cred->suid) {
				return AUTH_DECISION_DENY;
		}

		weight += 1;
		DONE_CHECK(actions)
	}

	if (actions & AUTH_ACTIONS_CRED_SETEGID) {
		if (CRED_IS_ESU(cred)) {
			weight += 1;
		} else if (*eid != cred->gid && *eid != cred->sgid) {
				return AUTH_DECISION_DENY;
		}

		weight += 1;
		DONE_CHECK(actions)
	}

	done:
	return weight ? AUTH_DECISION_ALLOW : AUTH_DECISION_DEFER;
}

static int process(cred_t *cred, int actions, void *arg0, void *arg1, void *arg2) {
	proc_t *proc = arg0;
	int *signal = arg1;

	int weight = 0;
	if (actions & AUTH_ACTIONS_PROCESS_SIGNAL) {
		if (CRED_IS_ESU(cred))
			weight += 1;
		else if (cred->uid == proc->cred.uid ||
				cred->uid == proc->cred.suid ||
				cred->euid == proc->cred.uid ||
				cred->euid == proc->cred.suid)
			weight += 1;
		else if (*signal == SIGCONT) {
			// TODO SIGCONT is supposed to work on the same process group; we are being a bit more permissive here for now
			weight += 1;
		}

		DONE_CHECK(actions);
	}

	done:
	return weight ? AUTH_DECISION_ALLOW : AUTH_DECISION_DEFER;
}

void defaultauth_init() {
	auth_registerlistener(AUTH_SCOPE_FILESYSTEM, filesystem);
	auth_registerlistener(AUTH_SCOPE_SYSTEM, system);
	auth_registerlistener(AUTH_SCOPE_CRED, credlistener);
	auth_registerlistener(AUTH_SCOPE_PROCESS, process);
}
