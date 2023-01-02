#include <stdio.h>

__attribute__ ((format (sprintf, 1, 2))) int sprintf (char* s, const char* format, ...)
{
    va_list list;
    va_start (list, format);
    int i = vsprintf (s, format, list);
    va_end (list);
    return i;

}
