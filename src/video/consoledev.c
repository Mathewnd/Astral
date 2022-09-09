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

}

__attribute__((noreturn)) static void console_thread(){

	for(;;){
		kbpacket_t packet;

		keyboard_getandwait(0, &packet);


		if(packet.ascii && (KBPACKET_FLAGS_RELEASED & packet.flags) == 0){

			// echo		

			console_write(&packet.ascii, 1);

			arch_interrupt_disable();

			ringbuffer_write(&input, &packet.ascii, 1);
		
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

	if(ringbuffer_init(&input, 4096))
		_panic("Failed to initialise console ringbuffer", 0);

	thread = sched_newkthread(console_thread, 4096*10, true, THREAD_PRIORITY_KERNEL);

	if(!thread)
		_panic("Failed to initialise console thread", 0);
	
}
