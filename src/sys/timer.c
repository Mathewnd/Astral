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
	
	arch_getcls()->timerfirstreq = iter;

	while(iter){
		iter->ticks -= pendingreq->ticks;
		iter = iter->next;
	}

	pendingreq->func(ctx, pendingreq->argptr);
	
	if(!arch_getcls()->timerfirstreq) return;

	arch_cputimer_fire(arch_getcls()->timerfirstreq->ticks);

}

void timer_resume(){
	arch_cputimer_fire(arch_getcls()->timerfirstreq->ticks);
}

void timer_stop(){
	
	size_t remainingticks = arch_cputimer_stop();
	timer_req* iter = arch_getcls()->timerfirstreq;
	size_t subticks = iter->ticks - remainingticks;
	while(iter){
		iter->ticks -= subticks;
		iter = iter->next;
	}
}

void timer_add(timer_req* req, size_t us){
	
	size_t remainingticks = arch_cputimer_stop();

	timer_req* iter = arch_getcls()->timerfirstreq;

	
	req->ticks = us*arch_getcls()->timerticksperus;

	if(!iter){
		arch_getcls()->timerfirstreq = req;
		req->next = NULL;
		arch_cputimer_fire(req->ticks);
		printf("ticks3: %lu", req->ticks);
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
		arch_cputimer_fire(req->ticks);
		printf("ticks2: %lu", req->ticks);
		return;
	}

	while(iter->next && iter->next->ticks <= req->ticks)
		iter = iter->next;
	
	req->next = iter->next;
	iter->next = req;

	printf("ticks: %lu", arch_getcls()->timerfirstreq->ticks);
	arch_cputimer_fire(arch_getcls()->timerfirstreq->ticks);
	
}
