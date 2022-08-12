section .text

%macro pushregs 0



	push rbp
	push rsi
	push rdi
	push r15
	push r14
	push r13
	push r12
	push r11
	push r10
	push r9
	push r8
	push rdx
	push rcx
	push rbx
	push rax
	
	mov rax, ds
	push rax
	mov rax, es
	push rax
	mov rax, fs
	push rax
	mov rax, gs
	push rax

	cmp rax,0x30
	jmp .noswapgs
	swapgs
	.noswapgs:

	mov rdi,cr2
	push rdi


%endmacro

%macro popregs 0 
	
	add rsp, 8 ; cr2
	
	pop rax
	cmp rax,0x30
	je .dontpopsegs
	mov gs,rax
	pop rax
	mov fs,rax
	pop rax
	mov es,rax
	pop rax
	mov ds,rax
	swapgs
	jmp .poprest
	.dontpopsegs:
	add rsp,24
	
	.poprest:
	pop rax
	pop rbx
	pop rcx
	pop rdx
	pop r8
	pop r9
	pop r10
	pop r11
	pop r12
	pop r13
	pop r14
	pop r15
	pop rdi
	pop rsi
	pop rbp


%endmacro

%macro isr 2
	
	global %1
	%1:
	
	push qword 0 ; error code for regs struct

	pushregs

	mov rdi,rsp

	cld
	extern %2
	call %2
	
	popregs
	
	add rsp,8 ; remove error code

	iretq
	
	
%endmacro




%macro except 2

	global %1
	%1:

	pushregs
	
	mov rdi,rsp
	
	cld
	extern %2
	call %2
	
	popregs

	add rsp,8 ; remove error code

	iretq
%endmacro

extern asmisr_panic
asmisr_panic:
	
	cli
	.hlt:
	hlt
	jmp .hlt ; NMI stuff

except	asmisr_pagefault, isr_pagefault
isr	asmisr_general, isr_general
except	asmisr_except, isr_except
isr	asmisr_lapicnmi, isr_lapicnmi
isr	asmisr_mmuinval, isr_mmuinval
isr	asmisr_timer, isr_timer
