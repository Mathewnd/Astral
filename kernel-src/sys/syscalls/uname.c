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
static char *sysname = "Astral";
static char *release = "Astral";

syscallret_t syscall_uname(context_t *, utsname_t *utsname) {
	syscallret_t ret = {0};

	char hostname[HOST_NAME_MAX + 1];
	hostname_get(hostname);

	if (
	usercopy_touser(utsname->sysname, sysname, strlen(sysname) + 1) ||
	usercopy_touser(utsname->nodename, hostname, strlen(hostname) + 1) ||
	usercopy_touser(utsname->release, release, strlen(release) + 1) ||
	usercopy_touser(utsname->version, BUILD_DATE, strlen(BUILD_DATE) + 1) ||
	usercopy_touser(utsname->machine, BUILD_TARGET, strlen(BUILD_TARGET) + 1) ||
	usercopy_touser(utsname->domainname, hostname, strlen(hostname) + 1))
		ret.errno = EFAULT;

	return ret;
}
