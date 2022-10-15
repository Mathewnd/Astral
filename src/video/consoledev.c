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

// taken from https://en.wikipedia.org/wiki/ANSI_escape_code#Terminal_input_sequences

#define HOME_STR "\e[1~"
#define INSERT_STR "\e[2~"
#define DELETE_STR "\e[3~"
#define END_STR "\e[4~"
#define PGUP_STR "\e[5~"
#define PGDN_STR "\e[6~"

#define UP_STR "\e[A"
#define DOWN_STR "\e[B"
#define RIGHT_STR "\e[C"
#define LEFT_STR "\e[D"

__attribute__((noreturn)) static void console_thread(){
	
	char buff[THREAD_BUFF_MAX];
	int buffpos = 0;

	for(;;){
		kbpacket_t packet;

		keyboard_getandwait(0, &packet);

		if(KBPACKET_FLAGS_RELEASED & packet.flags)
			continue;

		char* strbufptr;
		size_t strbuflen;


		if(packet.ascii){
			strbufptr = &packet.ascii;
			strbuflen = 1;
		}
		else{
			switch(packet.keycode){
				case KEYCODE_HOME:
					strbufptr = HOME_STR;
					break;
				case KEYCODE_INSERT:
					strbufptr = INSERT_STR;
					break;
				case KEYCODE_DELETE:
					strbufptr = DELETE_STR;
					break;
				case KEYCODE_END:
					strbufptr = END_STR;
					break;
				case KEYCODE_PAGEUP:
					strbufptr = PGUP_STR;
					break;
				case KEYCODE_PAGEDOWN:
					strbufptr = PGDN_STR;
					break;
				case KEYCODE_UP:
					strbufptr = UP_STR;
					break;
				case KEYCODE_DOWN:
					strbufptr = DOWN_STR;
					break;
				case KEYCODE_RIGHT:
					strbufptr = RIGHT_STR;
					break;
				case KEYCODE_LEFT:
					strbufptr = LEFT_STR;
					break;
				default:
					continue;
			}
			strbuflen = strlen(strbufptr);
		}


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
			
			if(packet.ascii && tty.c_lflag & ECHO)
				console_write(strbufptr, strbuflen);
			
			
			for(size_t i = 0; i < strbuflen; ++i){
				buff[buffpos++] = strbufptr[i];
				if(buffpos == THREAD_BUFF_MAX){
					flush = true;
					break;
				}
			}

			if(flush){
				arch_interrupt_disable();

				ringbuffer_write(&input, buff, buffpos);
			
				arch_interrupt_enable();
				
				buffpos = 0;

				event_signal(&inputevent, true);
			}
		}
		else{ // not canon
			if(tty.c_lflag & ECHO){
				console_write(strbufptr, strbuflen);
			}
			arch_interrupt_disable();

			ringbuffer_write(&input, strbufptr, strbuflen);

			arch_interrupt_enable();

			event_signal(&inputevent, true);

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

#include <arch/cls.h>

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

#include <poll.h>

static int poll(int minor, pollfd* fd){
	
	if((fd->events & POLLIN) && input.write != input.read)
		fd->revents |= POLLIN;
	
	if((fd->events & POLLOUT) && outputlock == 0)
		fd->revents |= POLLOUT;


	return 0;
	
}

devcalls calls = {
	read, write, isatty, isseekable, ioctl, poll
};

static void callback(struct limine_terminal *terminal, uint64_t type, uint64_t arg1, uint64_t arg2, uint64_t arg3){
	asm("cli;hlt");
}

void consoledev_init(){
	
	if(devman_newdevice("console", TYPE_CHARDEV, MAJOR_CONSOLE, 0, &calls))
	_panic("Failed to create console device", 0);

	if(ringbuffer_init(&input, THREAD_BUFF_MAX))
		_panic("Failed to initialise console ringbuffer", 0);

	thread = sched_newkthread(console_thread, 4096*10, true, THREAD_PRIORITY_KERNEL);

	if(!thread)
		_panic("Failed to initialise console thread", 0);

	tty.c_lflag = ECHO | ICANON | ISIG;
	tty.c_cc[VINTR] = 0x03;
    	tty.ibaud = 38400;
    	tty.obaud = 38400;

	tty.c_cc[VMIN] = 1;
	
	liminetty_setcallback(0, callback);

}
