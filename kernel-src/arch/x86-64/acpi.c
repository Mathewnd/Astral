#include <limine.h>
#include <kernel/pmm.h>
#include <stdint.h>

#include <uacpi/kernel_api.h>

static volatile struct limine_rsdp_request rsdp_request = {
	.id = LIMINE_RSDP_REQUEST_ID,
	.revision = 0
};

uacpi_status uacpi_kernel_get_rsdp(uacpi_phys_addr *rsdp_out) {
	*rsdp_out = (uintptr_t)FROM_HHDM(rsdp_request.response->address);
	return UACPI_STATUS_OK;
}
