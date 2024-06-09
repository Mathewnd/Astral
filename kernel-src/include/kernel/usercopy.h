#ifndef _USERCOPY_H
#define _USERCOPY_H

#include <stddef.h>
#include <stdint.h>
#include <string.h>

//IS_USER_ADDRESS(a) is provided by arch/mmu.h
#include <arch/mmu.h>

static inline int _usercopy_memcpy_wrapper(void *dst, void *src, size_t size) {
	memcpy(dst, src, size);
	return 0;
}
static inline int _usercopy_strlen_wrapper(void *str, size_t *size) {
	*size = strlen(str);
	return 0;
}

#define USERCOPY_POSSIBLY_FROM_USER(kernel, user, size) (IS_USER_ADDRESS(user) ? usercopy_fromuser(kernel, user, size) : _usercopy_memcpy_wrapper(kernel, user, size))
#define USERCOPY_POSSIBLY_TO_USER(user, kernel, size) (IS_USER_ADDRESS(user) ? usercopy_touser(user, kernel, size) : _usercopy_memcpy_wrapper(user, kernel, size))
#define USERCOPY_POSSIBLY_STRLEN_FROM_USER(user, sizep) (IS_USER_ADDRESS(user) ? usercopy_strlen(user, sizep) : _usercopy_strlen_wrapper(user, sizep))

int usercopy_touser(void *user, void *kernel, size_t size);
int usercopy_fromuser(void *kernel, void *user, size_t size);
int usercopy_fromuseratomic32(uint32_t *user32, uint32_t *value);
int usercopy_strlen(const char *, size_t *size);

#endif
