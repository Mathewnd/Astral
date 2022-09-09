global arch_switchcontext
arch_switchcontext:
	mov rsp,rdi
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

	iretq
