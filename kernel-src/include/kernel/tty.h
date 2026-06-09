#ifndef _TTY_H
#define _TTY_H

#include <mutex.h>
#include <termios.h>
#include <kernel/poll.h>
#include <ringbuffer.h>
#include <kernel/vfs.h>

#define TTY_DEVICE_BUFFER_SIZE 512
#define TTY_READ_BUFFER_SIZE 4096

typedef size_t (*ttydevicewritefn_t)(void *internal, char *str, size_t size);
typedef void (*ttyinactivefn_t)(void *internal);
typedef int (*tty_hup_check_t)(void *internal);
typedef void (*tty_termios_callback_t)(void *internal, termios_t *termios);

typedef struct {
	pollheader_t pollheader;
	char *name;
	ringbuffer_t readbuffer;
	mutex_t readmutex;
	mutex_t writemutex;
	termios_t termios;
	void *deviceinternal;
	char *devicebuffer;
	int devicepos;
	ttydevicewritefn_t writetodevice;
	ttyinactivefn_t inactivedevice;
	tty_termios_callback_t termios_callback;
	tty_hup_check_t hup_check;
	int minor;
	winsize_t winsize;
	vnode_t *mastervnode;
	spinlock_t sessionlock;
	struct proc_t *session;
	size_t opened;
	bool has_been_opened;
} tty_t;

void tty_init();
tty_t *tty_create(char *name, ttydevicewritefn_t writefn, ttyinactivefn_t inactivefn, tty_termios_callback_t termios_callback, tty_hup_check_t hup_check, void *internal);
void tty_process(tty_t *tty, char c);
void tty_unregister(tty_t *tty);
int tty_ioctl(tty_t *tty, unsigned long req, void *arg, int *result, cred_t *cred);
size_t tty_opened(tty_t *tty);
void tty_release(tty_t *tty);

#endif
