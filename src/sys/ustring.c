#include <kernel/ustring.h>
#include <arch/cls.h>

extern void arch_u_strlen_cont();
extern int arch_u_strlen(const char* str, size_t* len);

int u_strlen(const char* str, size_t* len){
    
	arch_getcls()->thread->umemopfailaddr = arch_u_strlen_cont;

	int ret = arch_u_strlen(str, len);

	arch_getcls()->thread->umemopfailaddr = NULL;

	return ret == 0 ? 0 : arch_getcls()->thread->umemoperror;
    
}


extern void arch_u_memcpy_cont();
extern int arch_u_memcpy(void* dest, const void* src, size_t len);

int u_memcpy(void* dest, const void* src, size_t len){
    
	arch_getcls()->thread->umemopfailaddr = arch_u_memcpy_cont;

	int ret = arch_u_memcpy(dest, src, len);

	arch_getcls()->thread->umemopfailaddr = NULL;

	return ret == 0 ? 0 : arch_getcls()->thread->umemoperror;
    
}

extern void arch_u_strcpy_cont();
extern int arch_u_strcpy(char* dest, const char* src);

int u_strcpy(char* dest, const char* src){
    
	arch_getcls()->thread->umemopfailaddr = arch_u_strcpy_cont;

	int ret = arch_u_strcpy(dest, src);

	arch_getcls()->thread->umemopfailaddr = NULL;

	return ret == 0 ? 0 : arch_getcls()->thread->umemoperror;
    
}
