#include <arch/ps2.h>
#include <assert.h>
#include <arch/idt.h>
#include <arch/apic.h>
#include <arch/cls.h>
#include <kernel/mouse.h>

#define MOUSE_CMD_SAMPLERATE 0xF3
#define MOUSE_CMD_REPORTDATA 0xF4

#define MOUSE 0
#define MOUSE_Z 3
#define MOUSE_5B 4

static bool haswheel = false;
static bool fivebuttons = false;

static void mouse_setrate(int port, uint8_t rate){
	
	device_write_response(port, MOUSE_CMD_SAMPLERATE);

	device_write_response(port, rate);

}

static uint8_t identify(int port){
	
	__assert(device_write_response(port, DEVICE_CMD_IDENTIFY) == ACK);

	bool timeout;

	uint8_t b = read_data_timeout(5, &timeout);
	
	__assert(!timeout);

	return b;
}

static int datac; 

uint8_t data[4];

static inline bool enoughdata(){
	if(!((haswheel && datac == 4) || (haswheel == false && datac == 3)))
		return false;
	else
		return true;
}

int mouseid;

void ps2mouse_irq(){

	data[datac] = inb(PS2_PORT_DATA);

	// check if the packet is bad

	if((data[0] & 8) == 0){
		datac = 0;
		return;
	}
		

	++datac;

	if(!enoughdata())
		return;

	mousepacket_t packet;

	datac = 0;
	bool left = data[0] & 1;
	bool right = data[0] & 2;
	bool middle = data[0] & 4;
	bool b4 = data[3] & 0x10;
	bool b5 = data[3] & 0x20;

	packet.x = data[1] - (data[0] & 0x10 ? 0x100 : 0);
	packet.y = data[2] - (data[0] & 0x20 ? 0x100 : 0);
	packet.z = (data[3] & 0x7) * (data[3] & 0x8 ? -1 : 1);
	
	packet.flags |= left ? MOUSE_FLAG_LB : 0;
	packet.flags |= middle ? MOUSE_FLAG_MB : 0;
	packet.flags |= right ? MOUSE_FLAG_RB : 0;
	packet.flags |= b4 ? MOUSE_FLAG_B4 : 0;
	packet.flags |= b5 ? MOUSE_FLAG_B5 : 0;

	mouse_packet(mouseid, packet);

	

}

void ps2mouse_init(){


	if(identify(2) != MOUSE){
		printf("Not a mouse!\n");
		return;
	}
	
	mouse_setrate(2, 200);
	mouse_setrate(2, 100);
	mouse_setrate(2, 80);
	
	if(identify(2) == MOUSE_Z){
		haswheel = true;

		mouse_setrate(2, 200);
		mouse_setrate(2, 200);
		mouse_setrate(2, 80);

		if(identify(2) == MOUSE_5B)
			fivebuttons = true;

	}


	mouse_setrate(2, 60); // TODO verify the best packet rate

	__assert(device_write_response(2, MOUSE_CMD_REPORTDATA) == ACK);
			
	ioapic_setlegacyirq(MOUSEIRQ, VECTOR_PS2MOUSE, arch_getcls()->lapicid, false);
	
	mouseid = mouse_getnew();

	__assert(mouseid != -1);

}
