section .text

global arch_sched_yieldtrampoline

; C sysv abi
; rdi -> thread ptr
; rsi -> regs ptr

REGSTRUCTSIZE equ 208

arch_sched_yieldtrampoline:

	; save return address to save as rip later
	pop r10

	; save rsp to reuse later
	mov r11, rsp
	
	; temporarily change rsp to set values for the regs structure

	mov rsp, rsi
	add rsp, REGSTRUCTSIZE

	; build the structure

	
	mov r9, ss
	push r9
	push r11 ; saved rsp
	pushfq
	mov r9, cs
	push r9
	push r10 ; rip
	push qword 0 ; err
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
	mov r9,ds
	push r9
	mov r9,es
	push r9
	push qword 0 ; fs
	push qword 0 ; gs
	mov r10, cr2
	push r10

	; restore old stack pointer

	mov rsp, r11
	
	extern sched_yieldtrampoline
	call sched_yieldtrampoline

	; the switch call never returns
