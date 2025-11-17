#include <arch/context.h>
#include <stdbool.h>
#include <arch/cpuid.h>
#include <kernel/slab.h>
#include <logging.h>

#define XCR0_X87 (1ull << 0)
#define XCR0_SSE (1ull << 1)
#define XCR0_AVX (1ull << 2)
#define XCR0_AVX512 (7ull << 5)

#define XCR0_MASK (XCR0_AVX512 | XCR0_AVX | XCR0_SSE | XCR0_X87)

static enum {
    FXSAVE,
    XSAVE,
    XSAVEOPT,
} xsave_method;
size_t arch_xsave_size;
static uint64_t xcr0_value;
static scache_t *xsave_cache;

typedef struct {
    uint16_t fcw;
    uint16_t fsw;
    uint8_t ftw;
    uint8_t reserved1;
    uint16_t fop;
    uint64_t fip;
    uint64_t fdp;
    uint32_t mxcsr;
    uint32_t mxcsr_mask;
    struct {
        uint64_t data[2];
    } mm[8];
    struct {
        uint64_t data[2];
    } xmm[16];
    uint64_t reserved2[6];
    uint64_t unused[6];
} fxsave_area_t;

void arch_extracontext_detect(void) {
    if (!arch_xsave_size) {
        if (cpuid_base_max_leaf() >= 0x0d) {
            xsave_method = XSAVE;

            cpuid_results_t results;
            cpuid_with_ecx(0x0d, 0, &results);
            xcr0_value = ((uint64_t)results.edx << 32) | results.eax;
            xcr0_value &= XCR0_MASK;
        } else {
            xsave_method = FXSAVE;
            arch_xsave_size = 512;
        }
    }

    if (xsave_method == FXSAVE) return;

    size_t cr4;
    asm volatile("mov %%cr4, %0" : "=r" (cr4));
    cr4 |= (1u << 18);
    asm volatile("mov %0, %%cr4" :: "r" (cr4));

    uint32_t low = xcr0_value;
    uint32_t high = xcr0_value >> 32;
    asm volatile("xsetbv" :: "a" (low), "d" (high), "c" (0u));

    if (!arch_xsave_size) {
        // after xsetbv ebx is updated to the size of the context for the enabled features
        cpuid_results_t results;
        cpuid_with_ecx(0x0d, 0, &results);
        arch_xsave_size = results.ebx;

        cpuid_with_ecx(0x0d, 1, &results);
        if (results.eax & 1) xsave_method = XSAVEOPT;
    }
}

int arch_extracontext_init(extracontext_t *context) {
    if (!xsave_cache) {
        xsave_cache = slab_newcache(arch_xsave_size, xsave_method == FXSAVE ? 16 : 64, NULL, NULL);
        __assert(xsave_cache);
    }

    context->xsave = slab_allocate(xsave_cache);
    if (!context->xsave) return 1;

    // all sse exceptions masked
    // initialise the x87 FPU state as it would be after the FNINIT instruction
    memset(context->xsave, 0, arch_xsave_size);
    fxsave_area_t *area = context->xsave;
    area->mxcsr = 0x1f80;
    area->fcw = 0x37f;

    return 0;
}

void arch_extracontext_free(extracontext_t *context) {
    slab_free(xsave_cache, context->xsave);
}

void arch_extracontext_save(extracontext_t *context) {
    context->gsbase = rdmsr(MSR_KERNELGSBASE);
    context->fsbase = rdmsr(MSR_FSBASE);

    switch (xsave_method) {
        case FXSAVE: asm("fxsaveq %0" ::"m"(*(char(*)[arch_xsave_size])context->xsave) : "memory"); break;
        case XSAVE: asm("xsaveq %0" ::"m"(*(char(*)[arch_xsave_size])context->xsave), "d"(-1), "a"(-1) : "memory"); break;
        case XSAVEOPT: asm("xsaveoptq %0" ::"m"(*(char(*)[arch_xsave_size])context->xsave), "d"(-1), "a"(-1) : "memory"); break;
    }
}

void arch_extracontext_load(extracontext_t *context) {
    wrmsr(MSR_KERNELGSBASE, context->gsbase);
    wrmsr(MSR_FSBASE, context->fsbase);

    switch (xsave_method) {
        case FXSAVE: asm("fxrstorq %0" ::"m"(*(char(*)[arch_xsave_size])context->xsave) : "memory"); break;
        case XSAVE:
        case XSAVEOPT: asm("xrstorq %0" ::"m"(*(char(*)[arch_xsave_size])context->xsave), "d"(-1), "a"(-1) : "memory"); break;
    }
}

void arch_extracontext_copy(extracontext_t *dst, const extracontext_t *src) {
    // XXX is this behavior correct? ucontext doesnt seem to save it on linux and so it will not be saved
    dst->gsbase = src->gsbase;
    dst->fsbase = src->fsbase;
    memcpy(dst->xsave, src->xsave, arch_xsave_size);
}
