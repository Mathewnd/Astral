extern void kmain();

extern void _start(){
	
	kmain();
	
	asm("cli;hlt;");

}
