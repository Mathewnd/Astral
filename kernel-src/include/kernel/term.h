#ifndef _TERM_H
#define _TERM_H

void term_init();
void term_putchar(char c);
void term_write(char *str, size_t count);
void term_getsize(size_t *x, size_t *y, size_t *fbx, size_t *fby);

#endif
