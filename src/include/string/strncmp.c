#include <stddef.h>

int strncmp(const char *a, const char *b, size_t c){

        int diff = 0;

        while(c-- && diff == 0)
                diff += *a++ - *b++;


        return diff;

}
