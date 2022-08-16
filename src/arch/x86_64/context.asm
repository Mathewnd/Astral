global arch_switchcontext
arch_switchcontext:
	mov rsp,rdi
	add rsp, 24 ; cr2 gs fs

	mov rbx,es
	pop rax
	cmp rax,rbx
	jne .segswap
	add rsp,8 ; ds
	jmp .genreg
	.segswap:
	swapgs
        mov es,rax
	pop rax
        mov ds,rax
	
	.genreg:
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
	
	iretq
