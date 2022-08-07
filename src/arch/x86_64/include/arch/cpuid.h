#ifndef _CPUID_H_INCLUDE
#define _CPUID_H_INCLUDE

#include <stdint.h>
#include <cpuid.h>

static inline uint64_t cpuid(){
    uint32_t eax, unused, edx;
    __get_cpuid(1, &eax, &unused, &unused, &edx);
    return ((uint64_t)edx << 32) + eax;
}

#endif
