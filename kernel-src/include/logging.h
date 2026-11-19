#ifndef _LOGGING_H
#define _LOGGING_H

#include <printf.h>
#include <panic.h>
#include <mutex.h>
#include <arch/cpu.h>
#include <util.h>

void arch_early_log(char c);
void _putchar(char c);
void logging_sethook(void (*fun)(char));
void logging_init();
extern mutex_t printf_mutex;

#ifdef printf
#undef printf
#endif

#define printf(...) { \
	if (likely(current_thread())) {\
		MUTEX_ACQUIRE(&printf_mutex); \
	} else { \
		while (MUTEX_TRY(&printf_mutex) == false) CPU_PAUSE(); \
	} \
	printf_(__VA_ARGS__); \
	MUTEX_RELEASE(&printf_mutex); \
}

#define __assert(x) \
	if (unlikely(!(x))){ \
		char reason[sizeof(__FILE__) + sizeof(__func__) + sizeof(#x) + 32]; \
		snprintf(reason, sizeof(reason), "\e[91m%s:%s:%d: %s failed\e[0m", __FILE__, __func__, __LINE__, #x); \
		_panic(reason, NULL); \
	};


#endif
