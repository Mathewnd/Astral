#include <stdio.h>
#include <arch/spinlock.h>

static int lock = 0;

__attribute__ ((format (printf, 1, 2))) int printf (const char* format, ...)
{
    spinlock_acquire(&lock);
    va_list list;
    va_start (list, format);
    int i = vprintf (format, list);
    va_end (list);
    spinlock_release(&lock);
    return i;

}
