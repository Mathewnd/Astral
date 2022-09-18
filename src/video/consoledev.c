#include <kernel/consoledev.h>
#include <kernel/devman.h>
#include <kernel/liminetty.h>
#include <errno.h>
#include <arch/panic.h>
#include <kernel/keyboard.h>
#include <ringbuffer.h>
#include <arch/interrupt.h>
#include <arch/spinlock.h>

#define CONSOLE_MAJOR 1

static ringbuffer_t input;
static thread_t* thread;
static event_t outputevent;
static event_t inputevent;
static int outputlock;

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

			bool flush = false;
			
			switch(packet.ascii){
				
				case '\b':
					if(buffpos){
						--buffpos;
						buff[buffpos] = '\0';
						console_write(BACKSPACE_STR, strlen(BACKSPACE_STR));
					}
					continue;
				case '\n':
					flush = true;
					break;
			}

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
	}

}

static int isatty(int minor){
	return 0;
}

static int read(int *error, int dev, void* buff, size_t count, size_t offset){
	
	int readc = 0;

	*error = 0;

	while(readc == 0){
		
		arch_interrupt_disable();

		readc = ringbuffer_read(&input, buff, count);
		
		arch_interrupt_enable();	
		
		if(!readc) *error = event_wait(&inputevent, true);
		
		if(*error)
			return -1;

		

	}

	return readc;
}

static int write(int *error, int dev, void* buff, size_t count, size_t offset){
	*error = 0;
	
	console_write(buff, count);
	
	return count;
}

devcalls calls = {
	read, write, isatty
};


void consoledev_init(){
	
	if(devman_newdevice("console", TYPE_CHARDEV, CONSOLE_MAJOR, 0, &calls))
	_panic("Failed to create console device", 0);

	if(ringbuffer_init(&input, THREAD_BUFF_MAX))
		_panic("Failed to initialise console ringbuffer", 0);

	thread = sched_newkthread(console_thread, 4096*10, true, THREAD_PRIORITY_KERNEL);

	if(!thread)
		_panic("Failed to initialise console thread", 0);
	
}
