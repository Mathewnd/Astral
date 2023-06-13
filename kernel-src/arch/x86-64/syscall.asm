section .rodata
extern syscall_print
extern syscall_mmap
extern syscall_openat
extern syscall_read
extern syscall_seek
extern syscall_close
extern syscall_archctl
extern syscall_write
extern syscall_getpid
extern syscall_fstat
extern syscall_fstatat
extern syscall_fork
extern syscall_execve
extern syscall_exit
extern syscall_waitpid
extern syscall_munmap
syscalltab:
dq syscall_print
dq syscall_mmap
dq syscall_openat
dq syscall_read
dq syscall_seek
dq syscall_close
dq syscall_archctl
dq syscall_write
dq syscall_getpid
dq syscall_fstat
dq syscall_fstatat
dq syscall_fork
dq syscall_execve
dq syscall_exit
dq syscall_waitpid
dq syscall_munmap
syscallcount equ 16
section .text
global arch_syscall_entry
; on entry:
; rcx has the return address
; r11 has the old rflags
; syscall arguments:
; rax has the syscall number
; rdi has the first argument
; rsi has the second argument
; rdx has the third argument
; r10 has the fourth argument
; r8 has the fifth argument
; r9 has the sixth argument
; return:
; rdx has errno
; rax has return value
arch_syscall_entry:
	swapgs
	; save return address on user stack
	push rcx
	mov rcx, [gs:0x0] ; thread pointer
	mov rcx, [rcx] ; kernel stack top
	xchg rsp, rcx ; switch stack pointers

	; push context
	push qword 0x1b ; user SS
	push qword rcx  ; user RSP
	add qword [rsp], 8 ; correct the old stack pointer to before the return address push
	push r11     ; rflags is stored in r11
	push qword 0x23 ; user CS
	push qword [rcx] ; save return RIP
	push qword 0 ; error
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
	mov rbx, ds
	push rbx
	mov rbx, es
	push rbx
	mov rbx, fs
	push rbx
	mov rbx, gs
	push rbx
	mov rbx, cr2
	push rbx

	; prepare arguments for the functions
	push r9
	mov r9, r8
	mov r8, r10
	mov rcx, rdx
	mov rdx, rsi
	mov rsi, rdi
	mov rdi, rsp
	add rdi, 8 ; context pointer is after r9 argument
	sti

	cmp rax, syscallcount
	jae .nosyscall
	call [syscalltab + rax * 8]
	jmp .return
	.nosyscall:
	extern syscall_invalid
	call syscall_invalid
	.return:
	cli
	; restore context
	add rsp, 32 ; r9 argument, cr2, gs, and fs are not popped.
	pop rbx
	mov es,rbx
	pop rbx
	mov ds,rbx
	add rsp, 8 ; rax
	pop rbx
	pop rcx
	add rsp, 8 ; rdx
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
	add rsp, 8 ; remove error code
	pop rcx
	add rsp, 8 ; user CS
	pop r11    ; rflags
	pop rsp
	swapgs
	o64 sysret
