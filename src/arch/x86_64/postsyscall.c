#include <kernel/sched.h>

void postsyscall(){
	sched_threadexitcheck();
}
