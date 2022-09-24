
void nulldev_init();
void zerodev_init();
void fulldev_init();

void pseudodevs_init(){
	
	nulldev_init();
	zerodev_init();
	fulldev_init();
}
