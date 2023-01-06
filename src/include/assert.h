#ifndef _ASSERT_H_INCLUDE
#define _ASSERT_H_INCLUDE

#include <arch/panic.h>
#include <stdio.h>

#define __assert(condition) if(!(condition) && printf("in file %s in func %s in line %d: assert %s failed\n", __FILE__, __func__, __LINE__, #condition)) _panic("Assert failed", NULL);

#endif
