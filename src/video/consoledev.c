#include <kernel/consoledev.h>
#include <kernel/devman.h>
#include <kernel/liminetty.h>
#include <errno.h>
#include <arch/panic.h>
#include <kernel/keyboard.h>
#include <ringbuffer.h>
#include <arch/interrupt.h>
#include <arch/spinlock.h>
#include <termios.h>

static ringbuffer_t input;
static thread_t* thread;
static event_t outputevent;
static event_t inputevent;
static int outputlock;
static termios tty;

void console_write(char* str, size_t count){
	
	
	while(!spinlock_trytoacquire(&outputlock)){
			event_wait(&outputevent, false);
	}

	liminetty_writeuser(str, count);

	spinlock_release(&outputlock);

	event_signal(&outputevent, true);

}

#define THREAD_BUFF_MAX 2048
#define BACKSPACE_STR "\b \b"
__attribute__((noreturn)) static void console_thread(){
	
	char buff[THREAD_BUFF_MAX];
	int buffpos = 0;

	for(;;){
		kbpacket_t packet;

		keyboard_getandwait(0, &packet);

		if(packet.ascii && (KBPACKET_FLAGS_RELEASED & packet.flags) == 0){
			if(tty.c_lflag & ICANON){
				bool flush = false;
				
				switch(packet.ascii){
					
					case '\b':
						if(buffpos){
							--buffpos;
							buff[buffpos] = '\0';
							if(tty.c_lflag & ECHO)
								console_write(BACKSPACE_STR, strlen(BACKSPACE_STR));
						}
						continue;
					case '\n':
						flush = true;
						break;
				}
				
				if(tty.c_lflag & ECHO)
					console_write(&packet.ascii, 1);
				
				buff[buffpos++] = packet.ascii;

				if(buffpos == THREAD_BUFF_MAX)
					flush = true;

				if(flush){
					arch_interrupt_disable();

					ringbuffer_write(&input, buff, buffpos);
				
					arch_interrupt_enable();
					
					buffpos = 0;

					event_signal(&inputevent, true);
				}
			}
			else{ // not canon
				
				if(tty.c_lflag & ECHO)
					console_write(&packet.ascii, 1);

				arch_interrupt_disable();

                                ringbuffer_write(&input, &packet.ascii, 1);

                                arch_interrupt_enable();

				event_signal(&inputevent, true);


			}
		}
	}

}

static int isatty(int minor){
	return 0;
}

static int read(int *error, int dev, void* buff, size_t count, size_t offset){
	
	int readc = 0;

	*error = 0;

	while(readc < count){
		
		arch_interrupt_disable();

		readc += ringbuffer_read(&input, buff+readc, count-readc);
		
		arch_interrupt_enable();	
		
		if(readc < count && readc < tty.c_cc[VMIN])
			*error = event_wait(&inputevent, true);
		
		if(*error)
			return -1;

		if(readc >= tty.c_cc[VMIN])
			break;

	}

	return readc;
}

static int write(int *error, int dev, void* buff, size_t count, size_t offset){
	*error = 0;
	
	console_write(buff, count);
	
	return count;
}


static int isseekable(){
	return ESPIPE;
}

#define TCGETS 0x5401
#define TCSETS 0x5402
#define TIOCGWINSZ 0x5413

typedef struct{
    unsigned short ws_row;
    unsigned short ws_col;
    unsigned short ws_xpixel;
    unsigned short ws_ypixel;
} winsize;

static int ioctl(int minor, unsigned long req, void* arg){
	switch(req){
		case TIOCGWINSZ:
			struct limine_terminal* ltty = liminetty_get(0);
			winsize* w = arg;
			w->ws_row = ltty->rows;
			w->ws_col = ltty->columns;
			break;
		case TCGETS:
			*(termios*)arg = tty;
			break;
		case TCSETS:
			tty = *(termios*)arg;
			break;
		default:
			return ENOTTY;
	}

	return 0;

}

devcalls calls = {
	read, write, isatty, isseekable, ioctl
};

void consoledev_init(){
	
	if(devman_newdevice("console", TYPE_CHARDEV, MAJOR_CONSOLE, 0, &calls))
	_panic("Failed to create console device", 0);

	if(ringbuffer_init(&input, THREAD_BUFF_MAX))
		_panic("Failed to initialise console ringbuffer", 0);

	thread = sched_newkthread(console_thread, 4096*10, true, THREAD_PRIORITY_KERNEL);

	if(!thread)
		_panic("Failed to initialise console thread", 0);

	tty.c_lflag = ECHO | ICANON;
	tty.c_cc[VMIN] = 1;
	
}
