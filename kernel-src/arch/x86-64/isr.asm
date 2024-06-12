section .text
%assign i 0
%rep 256

isr_%+ i:
	; push an error code if the cpu did not
	%if i <> 8 && i <> 10 && i <> 11 && i <> 12 && i <> 13 && i <> 14
		push qword 0
	%endif

	push rbp ; this push is here to free a register
	mov rbp, i
	jmp isr_common
%assign i i + 1
%endrep

isr_common:
	; check if swapgs is needed
	cmp qword [rsp+24], 0x23
	jne .notneeded1
	swapgs
	.notneeded1:
	; push context
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
	mov rdi,cr2
	push rdi

	; set up kernel segments
	mov rdi, 0x10
	mov ds, rdi
	mov es, rdi

	; save ctx pointer and interrupt vector number
	mov rdi, rbp ; vec
	mov rsi, rsp ; ctx

	cld
	extern interrupt_isr
	call interrupt_isr
	cli

	; pop context
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

section .rodata
global isr_table
isr_table:
	%assign i 0
	%rep 256
	dq isr_%+ i
	%assign i i + 1
	%endrep
