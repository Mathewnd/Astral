#include <kernel/mouse.h>
#include <kernel/devman.h>
#include <arch/panic.h>
#include <poll.h>
#include <arch/interrupt.h>

#define MOUSE_COUNT 32

static int mouseinuse;

mouse_t mouse[MOUSE_COUNT];
ringbuffer_t devbuffer;

void mouse_packet(int id, mousepacket_t packet){
	ringbuffer_write(&devbuffer, &packet, sizeof(mousepacket_t));
	ringbuffer_write(&mouse[id].buffer, &packet, sizeof(mousepacket_t));
	event_signal(&mouse[id].event, false);
}

int mouse_getnew(){
	
	int id;

	for(id = 0; id < 32; ++id)
		if(((mouseinuse >> id) & 1) == 0)
			break;

	if(id == 32)
		return -1;

	mouseinuse |= 1 << id;
	
	return id;
	
}

static int read(int *error, int minor, void* buff, size_t count, size_t offset){
        count /= sizeof(mousepacket_t);
        if(count == 0){
                *error = EINVAL;
                return -1;
        }

        arch_interrupt_disable();

        count = ringbuffer_read(&devbuffer, buff, count*sizeof(mousepacket_t));

        arch_interrupt_enable();

        *error = 0;
        return count;

}

static int poll(int minor, pollfd* fd){

        if((fd->events & POLLIN) && devbuffer.read != devbuffer.write)
                fd->revents |= POLLIN;


        return 0;
}


static devcalls calls = {
	.read = read,
	.poll = poll
};

void mouse_init(){
	
	for(uintmax_t i = 0; i < MOUSE_COUNT; ++i){
		if(ringbuffer_init(&mouse[i].buffer, sizeof(mousepacket_t)*100))
			_panic("Out of memory!", NULL);
	}

	if(ringbuffer_init(&devbuffer, sizeof(mousepacket_t)*100))
		_panic("Out of memory", NULL);
	
	if(devman_newdevice("mouse", TYPE_CHARDEV, MAJOR_MOUSE, 0, &calls))
		_panic("Failed to create mouse device", NULL);


}
