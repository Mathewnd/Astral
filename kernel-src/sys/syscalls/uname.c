#include <kernel/syscalls.h>
#include <build.h>

typedef struct {
	char sysname[65];
	char nodename[65];
	char release[65];
	char version[65];
	char machine[65];
	char domainname[65];
} utsname_t;

void hostname_get(char *buf);

syscallret_t syscall_uname(context_t *, utsname_t *utsname) {
	syscallret_t ret = {0};

	char hostname[HOST_NAME_MAX + 1];
	hostname_get(hostname);

	strcpy(utsname->sysname, "Astral");
	strcpy(utsname->nodename, hostname);
	strcpy(utsname->release, "Astral");
	strcpy(utsname->version, BUILD_DATE);
	strcpy(utsname->machine, BUILD_TARGET);
	strcpy(utsname->domainname, hostname);

	return ret;
}
