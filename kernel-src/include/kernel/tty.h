#ifndef _TTY_H
#define _TTY_H

#include <mutex.h>
#include <termios.h>
#include <kernel/poll.h>
#include <ringbuffer.h>
#include <kernel/vfs.h>

#define DEVICE_BUFFER_SIZE 512
#define READ_BUFFER_SIZE 4096

typedef void (*ttydevicewritefn_t)(char *str, size_t size);

typedef struct {
	pollheader_t pollheader;
	char *name;
	ringbuffer_t readbuffer;
	mutex_t readmutex;
	mutex_t writemutex;
	termios_t termios;
	char *devicebuffer;
	int devicepos;
	ttydevicewritefn_t writetodevice;
	int minor;
	winsize_t winsize;
	vnode_t *mastervnode;
} tty_t;

void tty_init();
tty_t *tty_create(char *name, ttydevicewritefn_t fn);
void tty_destroy(char *name, tty_t *tty);
void tty_process(tty_t *tty, char c);

#endif
