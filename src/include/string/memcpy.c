#include <stddef.h>
#include <stdint.h>

void* memcpy(void* d, void* s, size_t c){

        uint8_t* sc = (uint8_t*)s;
        uint8_t* dc = (uint8_t*)d;

        while(c--)
                *dc++ = *sc++;

        return d;

}

