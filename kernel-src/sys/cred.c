#include <kernel/cred.h>
#include <errno.h>
#include <logging.h>
#include <kernel/auth.h>

int cred_setuids(cred_t *cred, int uid, int euid, int suid) {
	if (uid < -1 || euid < -1 || suid < -1 || (uid == -1 && euid == -1 && suid == -1))
		return EINVAL;

	int error = auth_cred_check(cred, AUTH_ACTIONS_CRED_SETRESUID, uid, euid, suid);
	if (error)
		return error;

	cred->uid = (uid == -1) ? cred->uid : uid;
	cred->euid = (euid == -1) ? cred->euid : euid;
	cred->suid = (suid == -1) ? cred->suid : suid;

	return 0;
}

int cred_setgids(cred_t *cred, int gid, int egid, int sgid) {
	if (gid < -1 || egid < -1 || sgid < -1 || (gid == -1 && egid == -1 && sgid == -1))
		return EINVAL;

	int error = auth_cred_check(cred, AUTH_ACTIONS_CRED_SETRESGID, gid, egid, sgid);
	if (error)
		return error;

	cred->gid = (gid == -1) ? cred->gid : gid;
	cred->egid = (egid == -1) ? cred->egid : egid;
	cred->sgid = (sgid == -1) ? cred->sgid : sgid;

	return 0;
}

int cred_setuid(cred_t *cred, int uid) {
	if (uid < 0)
		return EINVAL;

	int error = auth_cred_check(cred, AUTH_ACTIONS_CRED_SETUID, uid, -1, -1);
	if (error)
		return error;

	if (CRED_IS_ESU(cred)) {
		cred->uid = uid;
		cred->euid = uid;
		cred->suid = uid;
	} else {
		cred->euid = uid;
	}

	return 0;
}

int cred_seteuid(cred_t *cred, int euid) {
	if (euid < 0)
		return EINVAL;

	int error = auth_cred_check(cred, AUTH_ACTIONS_CRED_SETEUID, -1, euid, -1);
	if (error)
		return error;

	cred->euid = euid;
	return 0;
}

int cred_setgid(cred_t *cred, int gid) {
	if (gid < 0)
		return EINVAL;

	int error = auth_cred_check(cred, AUTH_ACTIONS_CRED_SETGID, gid, -1, -1);
	if (error)
		return error;

	if (CRED_IS_ESU(cred)) {
		cred->gid = gid;
		cred->egid = gid;
		cred->sgid = gid;
	} else {
		cred->egid = gid;
	}

	return 0;
}

int cred_setegid(cred_t *cred, int egid) {
	if (egid < 0)
		return EINVAL;

	int error = auth_cred_check(cred, AUTH_ACTIONS_CRED_SETEGID, -1, egid, -1);
	if (error)
		return error;

	cred->egid = egid;
	return 0;
}

void cred_doexec(cred_t *cred, int suid, int sgid) {
	if (suid > -1)
		cred->euid = suid;

	if (sgid > -1)
		cred->egid = sgid;

	cred->suid = cred->euid;
	cred->sgid = cred->egid;
}

void cred_getuids(cred_t *cred, int *uidp, int *euidp, int *suidp) {
	*uidp = cred->uid;
	*euidp = cred->euid;
	*suidp = cred->suid;
}

void cred_getgids(cred_t *cred, int *gidp, int *egidp, int *sgidp) {
	*gidp = cred->gid;
	*egidp = cred->egid;
	*sgidp = cred->sgid;
}
