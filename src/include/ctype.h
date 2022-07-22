#ifndef _CTYPE_H_INCLUDE
#define _CTYPE_H_INCLUDE

static inline int isdigit(int n){
	return n >= '0' && n <= '9' ? 1 : 0;
}

#endif
