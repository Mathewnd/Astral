#ifndef _CONSOLE_H
#define _CONSOLE_H

#include <stddef.h>
#include <kernel/input.h>

void console_init();
void console_putc(char c);
size_t console_write(char *str, size_t size);
void console_process_events(input_event_t *events, int count);

#endif
