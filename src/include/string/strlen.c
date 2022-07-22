#include <stddef.h>

size_t strlen(const char* a){

        size_t s = 0;
        while(a[s] != '\0') ++s;
        return s;

}

