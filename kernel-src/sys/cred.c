#include <kernel/cred.h>
#include <errno.h>
#include <logging.h>

int cred_setuids(cred_t *cred, int uid, int euid, int suid) {
	if (uid < -1 || euid < -1 || suid < -1 || (uid == -1 && euid == -1 && suid == -1))
		return EINVAL;

	// TODO do permission check to see if its possible

	cred->uid = (uid == -1) ? cred->uid : uid;
	cred->euid = (euid == -1) ? cred->euid : euid;
	cred->suid = (suid == -1) ? cred->suid : suid;

	return 0;
}

int cred_setgids(cred_t *cred, int gid, int egid, int sgid) {
	if (gid < -1 || egid < -1 || sgid < -1 || (gid == -1 && egid == -1 && sgid == -1))
		return EINVAL;

	// TODO do permission check to see if its possible

	cred->gid = (gid == -1) ? cred->gid : gid;
	cred->egid = (egid == -1) ? cred->egid : egid;
	cred->sgid = (sgid == -1) ? cred->sgid : sgid;

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
