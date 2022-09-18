#include <kernel/timer.h>
#include <arch/cputimer.h>
#include <arch/cls.h>
#include <stdio.h>

void timer_init(){
	cls_t* cls = arch_getcls();
	cls->timerticksperus = arch_cputimer_init();
	printf("CPU%d: CPUTIMER tick: %lu per us\n", cls->lapicid, cls->timerticksperus);
}

void timer_irq(arch_regs* ctx){

	timer_req* pendingreq = arch_getcls()->timerfirstreq;
	timer_req* iter = pendingreq->next;

	if(arch_getcls()->timerpending){ // if we know we stopped, don't do anything yet
		while(iter){
			iter->ticks -= pendingreq->ticks;
			iter = iter->next;
		}
		return;
	}

	arch_getcls()->timerfirstreq = iter;
	


	while(iter){
		iter->ticks -= pendingreq->ticks;
		if(iter->ticks == 0){
			iter->func(ctx, iter->argptr);
			arch_getcls()->timerfirstreq = iter->next;
		}
		iter = iter->next;
	}

	pendingreq->func(ctx, pendingreq->argptr);
	
	if(!arch_getcls()->timerfirstreq) return;

	arch_cputimer_fire(arch_getcls()->timerfirstreq->ticks);

}

void timer_resume(){
	if(arch_getcls()->timerpending){ // if an int was pending, have it happen
		timer_req* iter = arch_getcls()->timerfirstreq;
		while(iter){
			iter->ticks++;
			iter = iter->next;
		}
		arch_getcls()->timerpending = false;
	}
	arch_cputimer_fire(arch_getcls()->timerfirstreq->ticks);
}

void timer_stop(){
	
	size_t remainingticks = arch_cputimer_stop();
	timer_req* iter = arch_getcls()->timerfirstreq;
	if(!iter) return;
	
	if(remainingticks == 0) // interrupt is pending
		arch_getcls()->timerpending = true;

	size_t subticks = iter->ticks - remainingticks;
	while(iter){
		iter->ticks -= subticks;
		iter = iter->next;
	}
}

void timer_add(timer_req* req, size_t us, bool start){
	size_t remainingticks = arch_cputimer_stop();

	timer_req* iter = arch_getcls()->timerfirstreq;

	
	req->ticks = us*arch_getcls()->timerticksperus;

	if(!iter){
		arch_getcls()->timerfirstreq = req;
		req->next = NULL;
		if(start);
			arch_cputimer_fire(req->ticks);
		return;
	}
	
	size_t subticks = iter->ticks - remainingticks;

	while(iter){
		iter->ticks -= subticks;
		iter = iter->next;
	}

	iter = arch_getcls()->timerfirstreq;
	
	if(iter->ticks > req->ticks){
		req->next = arch_getcls()->timerfirstreq;
		arch_getcls()->timerfirstreq = req;
		if(start)
			arch_cputimer_fire(req->ticks);
		return;
	}

	while(iter->next && iter->next->ticks <= req->ticks)
		iter = iter->next;
	
	req->next = iter->next;
	iter->next = req;

	if(start)
		arch_cputimer_fire(arch_getcls()->timerfirstreq->ticks);
	
}
