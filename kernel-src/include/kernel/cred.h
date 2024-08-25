#ifndef _CRED_H
#define _CRED_H

#include <stdint.h>

typedef struct {
	int uid, euid, suid;
	int gid, egid, sgid;
} cred_t;

#define CRED_SUPERUSER 0
#define CRED_IS_SU(cred) ((cred)->uid == CRED_SUPERUSER)
#define CRED_IS_ESU(cred) ((cred)->euid == CRED_SUPERUSER)
#define CRED_IS_SSU(cred) ((cred)->suid == CRED_SUPERUSER)

int cred_setuids(cred_t *cred, int uid, int euid, int suid);
int cred_setgids(cred_t *cred, int uid, int euid, int suid);
int cred_setuid(cred_t *cred, int uid);
int cred_seteuid(cred_t *cred, int euid);
int cred_setgid(cred_t *cred, int gid);
int cred_setegid(cred_t *cred, int egid);
void cred_getuids(cred_t *cred, int *uidp, int *euidp, int *suidp);
void cred_getgids(cred_t *cred, int *gidp, int *egidp, int *sgidp);
void cred_doexec(cred_t *cred, int suid, int sgid);

#endif
