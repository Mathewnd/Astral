#ifndef _CRED_H
#define _CRED_H

#define CRED_PERM_SUPERUSER (1l << 63)

#include <stdint.h>

typedef struct {
	int uid, euid, suid;
	int gid, egid, sgid;

	uintmax_t allow;
	uintmax_t deny;
} cred_t;

int cred_setuids(cred_t *cred, int uid, int euid, int suid);
int cred_setgids(cred_t *cred, int uid, int euid, int suid);
void cred_getuids(cred_t *cred, int *uidp, int *euidp, int *suidp);
void cred_getgids(cred_t *cred, int *gidp, int *egidp, int *sgidp);

#endif
