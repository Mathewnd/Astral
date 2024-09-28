#include <limine.h>
#include <kernel/pmm.h>
#include <stdint.h>

#include <uacpi/kernel_api.h>

static volatile struct limine_rsdp_request rsdpreq = {
	.id = LIMINE_RSDP_REQUEST,
	.revision = 0
};

uacpi_status uacpi_kernel_get_rsdp(uacpi_phys_addr *outrsdp) {
	*outrsdp = (uintptr_t)FROM_HHDM(rsdpreq.response->address);
	return UACPI_STATUS_OK;
}
