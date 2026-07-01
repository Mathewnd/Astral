#include <kernel/syscalls.h>
#include <arch/cpu.h>
#include <kernel/page.h>
#include <kernel/mm.h>
#include <kernel/proc.h>

typedef struct {
	long uptime;
	unsigned long loads[3];
	unsigned long totalram;
	unsigned long freeram;
	unsigned long sharedram;
	unsigned long bufferram;
	unsigned long totalswap;
	unsigned long freeswap;
	unsigned short procs;
	unsigned long totalhigh;
	unsigned long freehigh;
	unsigned int mem_unit;
	char _f[20 - 2 * sizeof(long) - sizeof(int)];
} sysinfo_t;

syscallret_t syscall_sysinfo(context_t *, sysinfo_t *usysinfo) {
	syscallret_t ret = {
		.ret = -1
	};

	size_t total_pages, free_pages;
	mm_get_page_statistics(&total_pages, &free_pages);

	sysinfo_t sysinfo = {
		.uptime = timekeeper_timefromboot().s,
		.totalram = total_pages,
		.freeram = free_pages,
		.sharedram = 0,
		.bufferram = mm_cache_cached_pages,
		.totalswap = 0,
		.freeswap = 0,
		.procs = proc_get_count(),
		.totalhigh = 0,
		.freehigh = 0,
		.mem_unit = PAGE_SIZE
	};

	memset(sysinfo.loads, 0, sizeof(sysinfo.loads));
	memset(sysinfo._f, 0, sizeof(sysinfo._f));

	ret.errno = usercopy_touser(usysinfo, &sysinfo, sizeof(sysinfo));
	ret.ret = ret.errno ? -1 : 0;

	return ret;
}
