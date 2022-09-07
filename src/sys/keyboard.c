#include <kernel/keyboard.h>
#include <arch/panic.h>
#include <arch/interrupt.h>

#define MAX_KEYBOARD_COUNT 32

static uint32_t kblist;
static keyboard_t keyboards[MAX_KEYBOARD_COUNT];

void keyboard_packet(int kb, kbpacket_t packet){
	ringbuffer_write(&keyboards[kb].buffer, &packet, sizeof(kbpacket_t));
	event_signal(&keyboards[kb].event, false);
}

int keyboard_getandwait(int kb, kbpacket_t* buff){
	for(;;){
		
		arch_interrupt_disable();

		size_t readc = ringbuffer_read(&keyboards[kb].buffer, buff, sizeof(kbpacket_t));

		arch_interrupt_enable();

		if(readc)
			return 0;

		// interruptible wait

		int ret = event_wait(&keyboards[kb].event, true);
		if(ret)
			return ret;
	}
}

int keyboard_get(int kb, kbpacket_t* buff){
	
	arch_interrupt_disable();

	int readc = ringbuffer_read(&keyboards[kb].buffer, buff, sizeof(kbpacket_t));

	arch_interrupt_enable();

	return readc == 0;

}

int keyboard_getnew(){
	
	for(size_t i = 0; i < MAX_KEYBOARD_COUNT; ++i){
		if((kblist & (1 << i)) == 0){
			kblist |= 1 << i;
			return i;
		}
	}

	return -1;

}

void keyboard_init(){

	for(size_t i = 0; i < MAX_KEYBOARD_COUNT; ++i){
		if(ringbuffer_init(&keyboards[i].buffer, sizeof(kbpacket_t)*50))
			_panic("Out of memory", NULL);
	}

}
