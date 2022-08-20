#ifndef _LIMINETTY_H_INCLUDE
#define _LIMINETTY_H_INCLUDE

#include <limine.h>
#include <kernel/vmm.h>

void liminetty_write(int tty, const char* str, size_t len);
struct limine_framebuffer* liminetty_getfb(int tty);
void liminetty_setcallback(int tty, limine_terminal_callback callback);
struct limine_terminal* liminetty_get(int tty);
size_t liminetty_ttycount();
void   liminetty_setcontext(vmm_context* ctx);
void   liminetty_writeuser(const char* str, size_t len);
void   liminetty_writekernel(const char* str, size_t len);

#endif
