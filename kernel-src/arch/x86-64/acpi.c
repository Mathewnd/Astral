#include <limine.h>
#include <kernel/pmm.h>
#include <stdint.h>

static volatile struct limine_rsdp_request rsdpreq = {
	.id = LIMINE_RSDP_REQUEST,
	.revision = 0
};

uintptr_t arch_get_rsdp(void) {
	return (uintptr_t)FROM_HHDM(rsdpreq.response->address);
}
