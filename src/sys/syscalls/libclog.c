#include <kernel/syscalls.h>
#include <kernel/alloc.h>
#include <errno.h>
#include <string.h>
#include <stdio.h>

syscallret syscall_libc_log(char* str){
	
	syscallret ret;

	size_t len = strlen(str);

	char* buf = alloc(len + 2); // user strlen
	if(!buf){
		ret.errno = ENOMEM;
		ret.ret = -1;
		return ret;
	}

	strcpy(buf, str);

	buf[len] = '\n';

	console_write(buf, len+1);

	ret.errno = 0;
	ret.ret = 0;

	free(buf);

	return ret;
	
}
