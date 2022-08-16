#include <kernel/syscalls.h>
#include <kernel/alloc.h>
#include <errno.h>
#include <string.h>
#include <stdio.h>

syscallret syscall_libc_log(char* str){
	
	syscallret ret;

	char* buf = alloc(strlen(str) + 1);
	if(!buf){
		ret.errno = ENOMEM;
		ret.ret = -1;
		return ret;
	}

	strcpy(buf, str);

	printf("%s", str);

	ret.errno = 0;
	ret.ret = 0;

	free(buf);

	return ret;
	
}
