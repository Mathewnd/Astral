// logging.c
//
// provides a few preformatted logging functions for the kernel
//

#include <stdio.h>

// TODO
//
// allow it to be formated using a call for vprintf

void loginfo(char* str){
	printf("[\033[94mINFO\033[0m] %s", str);
}

void logwarn(char* str){	
	printf("[\033[93mWARN\033[0m] %s", str);
}

void logerror(char* str){
	printf("[\033[91mERROR\033[0m] %s", str);
}

void logfatal(char* str){
	printf("[\033[31mFATAL\033[0m] %s", str);
}
