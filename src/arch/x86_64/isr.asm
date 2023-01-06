section .text

%macro pushregs 0



	push rbp
	mov  rbp, [rsp+24] ; save CS for usermode checking
	; check and patch ss if it is invalid because of the syscall instruction
	cmp qword [rsp+48], 0x33
	jne .keepgoing
	mov qword [rsp+48], 0x43
	.keepgoing:
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
	


	
	cmp rbp,0x3b
	jne .noswapgs
	swapgs
	.noswapgs:

	mov rax, ds
	push rax
	mov rax, es
	push rax
	mov rax, fs
	push rax
	mov rax, gs
	push rax
	
	mov rdi,cr2
	push rdi


%endmacro

%macro popregs 0 
	
	add rsp, 24 ; cr2 gs fs
	
	pop rax
	mov es,rax
	pop rax
	mov ds,rax
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
	add rsp,8 ; remove error code

	cmp qword [rsp+8], 0x3b ; user cs
	jne .noswap

	swapgs

	.noswap:

%endmacro

%macro isr 3
	
	global %1
	%1:

	push qword 0 ; error code for regs struct

	pushregs

	mov rdi,rsp
	mov rsi,%3

	cld
	extern %2
	call %2
	
	popregs

	cmp qword [rsp+8], 0x3b
	jne .ok
	mov qword [rsp+32], 0x43
	.ok:

	iretq
	
	
%endmacro




%macro except 3

	global %1
	%1:

	pushregs
	
	mov rdi,rsp
	mov rsi,%3
	

	cld
	sti
	extern %2
	call %2
	cli

	popregs
	
	cmp qword [rsp+8], 0x3b
	jne .ok
	mov qword [rsp+32], 0x43
	.ok:

	iretq
%endmacro

extern asmisr_panic
asmisr_panic:
	
	cli
	.hlt:
	hlt
	jmp .hlt ; NMI stuff

except	asmisr_pagefault, isr_pagefault, 0xE
except  asmisr_gpf, isr_gpf, 0xD
except	asmisr_nm, isr_except, 0x7
isr	asmisr_general, isr_general, 0xFF
except	asmisr_except, isr_except, 0xFF
isr	asmisr_lapicnmi, isr_lapicnmi, 0x21
isr	asmisr_mmuinval, isr_mmuinval, 0x22
isr	asmisr_ps2mouse, isr_ps2mouse, 0x3F
isr 	asmisr_ps2kbd, isr_ps2kbd, 0x40
isr	asmisr_timer, isr_timer, 0x80
isr 	asmisr_simd, isr_simd, 0x13
