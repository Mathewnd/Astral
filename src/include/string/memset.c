#include <stddef.h>

void* memset(void* ptr, unsigned long value, size_t num){
        char* p = (char*)ptr;
        while(num--)
                *p++ = value;

        return ptr;
}
