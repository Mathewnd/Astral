section .text
global arch_context_switch
arch_context_switch:
	xor rsi, rsi ; is syscall? (no)
	extern sched_userspacecheck
	call sched_userspacecheck ; compiled with no_caller_saved_registers

	; rdi is the pointer to the context struct
	mov rsp, rdi

	add rsp, 24 ; cr2 gs and fs are not popped.
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

	; check if swapgs is needed
	cmp qword [rsp+8], 0x23
	jne .notneeded2
	swapgs
	.notneeded2:
	o64 iret

; saves context on the stack and passes as the argument to a function
; since this uses the C system V abi, some registers can be clobbered (in this case, r10 and r11)
; rdi has the pointer to the function to call
; rsi has the pointer to the stack the function should have on call. if NULL the stack is not changed
; if the main function returns, do a context switch as the registers could have been changed
global arch_context_saveandcall
arch_context_saveandcall:
	mov r11, [rsp] 	; save return address on scratch register
	mov r10, rsp
	add r10, 8 	; stack pointer before the call instruction

	test rsi,rsi
	jz .nostackchange
	mov rsp, rsi	; use desired stack
	.nostackchange:
	push qword 0x10 ; SS
	push r10 	; RSP
	pushf 		; movs and pushes don't affect flags
	push qword 0x08 ; CS
	push r11 	; return address
	sub rsp, 8 	; error code
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
	push qword 0x10 ; ES
	push qword 0x10 ; DS
	push qword 0    ; FS
	push qword 0	; GS
	sub rsp, 8 	; cr2


	mov r11, rdi 	; save function to call
	mov rdi, rsp 	; first argument is the context struct
	call r11 	; jump to the desired function

	; do context switch to return
	cli
	mov rdi, rsp
	call arch_context_switch
