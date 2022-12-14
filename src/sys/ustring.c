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


