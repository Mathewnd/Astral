#include <kernel/liminetty.h>
#include <arch/spinlock.h>
#include <kernel/console.h>
#include <arch/cls.h>
#include <kernel/alloc.h>
#include <arch/panic.h>

static struct limine_terminal** ttys;
static size_t ttycount;
static int usertty;   // accessed by /dev/console
static int kerneltty; // libc and kernel logs
static limine_terminal_write writetty;
static bool shared = false;
static int lock; // for shared access between user and kernel
static vmm_context* liminectx = NULL;
static limine_terminal_callback* callbacks = NULL;


static void callbackhandler(struct limine_terminal *terminal, uint64_t type, uint64_t arg1, uint64_t arg2, uint64_t arg3){
	uintmax_t tty = 0;
	for(;tty < ttycount; ++tty){
		if(ttys[tty] == terminal)
			break;
	}

	if(callbacks == NULL || tty == ttycount || callbacks[tty] == NULL);
		return;

	callbacks[tty](terminal, type, arg1, arg2, arg3);
	
}

static volatile struct limine_terminal_request req = {
	.id = LIMINE_TERMINAL_REQUEST,
	.revision = 0,
	.callback = callbackhandler
};

void liminetty_setcontext(vmm_context* ctx){
	liminectx = ctx;
}

void liminetty_setcallback(int tty, limine_terminal_callback callback){
	if(!callbacks){ // allocate space for the callbacks
		callbacks = alloc(sizeof(limine_terminal_callback)*ttycount);
		if(!callbacks)
			_panic("Failed to allocate callbacks", 0);
	}
	
	if(tty >= ttycount)
		return;

	callbacks[tty] = callback;

}	

struct limine_terminal* liminetty_get(int tty){
	if(tty >= ttycount)
		return NULL;
	
	return ttys[tty];
	
}

size_t liminetty_ttycount(){
	return ttycount;
}

void liminetty_write(int tty, const char* str, size_t len){
	if(tty >= ttycount) return;
	vmm_context* old = arch_getcls()->context;

	if(liminectx)
		vmm_switchcontext(liminectx);

	writetty(ttys[usertty], str, len);

	if(liminectx)
		vmm_switchcontext(old);

}

struct limine_framebuffer* liminetty_getfb(int tty){
	if(tty >= ttycount) return NULL;

	return ttys[tty]->framebuffer;

}

void liminetty_writeuser(const char* str, size_t len){
	
	if(shared)
		spinlock_acquire(&lock);
	
	liminetty_write(usertty, str, len);
	
	if(shared)
		spinlock_release(&lock);
	
}

void liminetty_writekernel(const char* str, size_t len){
	if(shared)
		spinlock_acquire(&lock);
	
	liminetty_write(kerneltty, str, len);
	
	if(shared)
		spinlock_release(&lock);
}


void liminetty_init(){
	if(!req.response)
		for(;;); // not a lot that can be done I guess
	
	struct limine_terminal_response* r = req.response;

	ttys = r->terminals;
	
	ttycount = r->terminal_count;
	
	usertty = 0;
	shared = ttycount == 1;
	kerneltty = shared ? 0 : 1;
	writetty = r->write;
	
	console_setwritehook(liminetty_writekernel);

}
