#include <kernel/keyboard.h>
#include <arch/panic.h>
#include <arch/interrupt.h>
#include <kernel/devman.h>

#define MAX_KEYBOARD_COUNT 32

// XXX this maybe should be strings rather than chars, as 
// some keys are sent to the applications in escape sequences

static char asciitable[] = {
       0, '\033', '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '-', '=', '\b', '\t', 'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p', '[', ']', '\n', 0, 'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', ';', '\'', '`', 0, '\\', 'z', 'x', 'c', 'v', 'b', 'n', 'm', ',', '.', '/', 0, '*', 0, ' ', 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, '7', '8', '9', '-', '4', '5', '6', '+', '4', '1', '2', '3', '0', '.', 0, 0, 0, 0, 0
};

static char asciitableupper[] = {
       0, '\033', '!', '@', '#', '$', '%', '^', '&', '*', '(', ')', '_', '+', '\b', '\t', 'Q', 'W', 'E', 'R', 'T', 'Y', 'U', 'I', 'O', 'P', '{', '}', '\n', 0, 'A', 'S', 'D', 'F', 'G', 'H', 'J', 'K', 'L', ':', '\"', '~', 0, '|', 'Z', 'X', 'C', 'V', 'B', 'N', 'M', '<', '>', '?', 0, '*', 0, ' ', 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, '7', '8', '9', '-', '4', '5', '6', '+', '4', '1', '2', '3', '0', '.', 0, 0, 0, 0, 0
};

static uint32_t kblist;
static keyboard_t keyboards[MAX_KEYBOARD_COUNT];

static ringbuffer_t devbuff;

void keyboard_packet(int kb, kbpacket_t packet){
	
	switch(packet.keycode){
		case KEYCODE_LEFTCTRL:
			if(packet.flags & KBPACKET_FLAGS_RELEASED)
				keyboards[kb].flags &= ~KBPACKET_FLAGS_LEFTCTRL;
			else
				keyboards[kb].flags |= KBPACKET_FLAGS_LEFTCTRL;
			break;
		case KEYCODE_RIGHTCTRL:
			if(packet.flags & KBPACKET_FLAGS_RELEASED)
                                keyboards[kb].flags &= ~KBPACKET_FLAGS_RIGHTCTRL;
                        else 
				keyboards[kb].flags |= KBPACKET_FLAGS_RIGHTCTRL;
			break;
		case KEYCODE_CAPSLOCK:
			if(packet.flags & KBPACKET_FLAGS_RELEASED)
                                keyboards[kb].flags &= ~KBPACKET_FLAGS_CAPSLOCK;
                        else
				keyboards[kb].flags |= KBPACKET_FLAGS_CAPSLOCK;
			break;
		case KEYCODE_LEFTALT:
			if(packet.flags & KBPACKET_FLAGS_RELEASED)
                                keyboards[kb].flags &= ~KBPACKET_FLAGS_LEFTALT;
                        else
				keyboards[kb].flags |= KBPACKET_FLAGS_LEFTALT;
			break;
		case KEYCODE_RIGHTALT:
			if(packet.flags & KBPACKET_FLAGS_RELEASED)
                                keyboards[kb].flags &= ~KBPACKET_FLAGS_RIGHTALT;
                        else
				keyboards[kb].flags |= KBPACKET_FLAGS_RIGHTALT;
			break;
		case KEYCODE_LEFTSHIFT:
			if(packet.flags & KBPACKET_FLAGS_RELEASED)
                                keyboards[kb].flags &= ~KBPACKET_FLAGS_LEFTSHIFT;
                        else
				keyboards[kb].flags |= KBPACKET_FLAGS_LEFTSHIFT;
			break;
		case KEYCODE_RIGHTSHIFT:
			if(packet.flags & KBPACKET_FLAGS_RELEASED)
                                keyboards[kb].flags &= ~KBPACKET_FLAGS_RIGHTSHIFT;
                        else
				keyboards[kb].flags |= KBPACKET_FLAGS_RIGHTSHIFT;
			break;	
	}
	
	packet.flags |= keyboards[kb].flags;

	char* table = asciitable;

	if(	(packet.flags & KBPACKET_FLAGS_LEFTSHIFT) ||
		(packet.flags & KBPACKET_FLAGS_RIGHTSHIFT))
		table = asciitableupper;

	packet.ascii = table[packet.keycode];
	ringbuffer_write(&devbuff, &packet, sizeof(kbpacket_t));	
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

static int write(int *error, int minor, void* buff, size_t count){*error = EINVAL; return -1;}
static int read(int *error, int minor, void* buff, size_t count, size_t offset){
	count /= sizeof(kbpacket_t);
	if(count == 0){
		*error = EINVAL;
		return -1;
	}
	
	arch_interrupt_disable();
	
	count = ringbuffer_read(&devbuff, buff, count*sizeof(kbpacket_t));

	arch_interrupt_enable();

	*error = 0;
	return count;

}

static int isatty(){
	return ENOTTY;
}

static int seekable(){
	return 0;
}

static devcalls calls = {
	read, write, isatty, seekable
};

void keyboard_init(){

	for(size_t i = 0; i < MAX_KEYBOARD_COUNT; ++i){
		if(ringbuffer_init(&keyboards[i].buffer, sizeof(kbpacket_t)*50))
			_panic("Out of memory", NULL);
	}

	if(ringbuffer_init(&devbuff, sizeof(kbpacket_t)*10)) _panic("Out of memory", NULL);
	
	if(devman_newdevice("keyboard", TYPE_CHARDEV, MAJOR_KB, 0, &calls))
		_panic("Failed to create keyboard device", 0);
	
}
