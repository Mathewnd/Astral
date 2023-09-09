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
extern syscall_getdents
extern syscall_dup
extern syscall_dup2
extern syscall_dup3
extern syscall_fcntl
extern syscall_chdir
extern syscall_pipe2
extern syscall_isatty
extern syscall_faccessat
extern syscall_unlinkat
extern syscall_ioctl
extern syscall_mkdirat
extern syscall_clockget
extern syscall_linkat
extern syscall_readlinkat
extern syscall_fchmod
extern syscall_fchmodat
extern syscall_umask
extern syscall_poll
extern syscall_nanosleep
extern syscall_ftruncate
extern syscall_mount
extern syscall_fchownat
extern syscall_utimensat
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
dq syscall_getdents
dq syscall_dup
dq syscall_dup2
dq syscall_dup3
dq syscall_fcntl
dq syscall_chdir
dq syscall_pipe2
dq syscall_isatty
dq syscall_faccessat
dq syscall_unlinkat
dq syscall_ioctl
dq syscall_mkdirat
dq syscall_clockget
dq syscall_linkat
dq syscall_readlinkat
dq syscall_fchmod
dq syscall_fchmodat
dq syscall_umask
dq syscall_poll
dq syscall_nanosleep
dq syscall_ftruncate
dq syscall_mount
dq syscall_fchownat
dq syscall_utimensat
syscallcount equ 40
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
	; saving the syscall number on cr2 is cursed but we need this extra register
	; and taking a page fault here would result in a triple fault anyways
	; because it's still using the user stack
	mov cr2, rax
	; rax can be used just fine now
	mov rax, [gs:0x0] ; thread pointer
	mov rax, [rax] ; kernel stack top
	xchg rsp, rax ; switch stack pointers

	; push context
	push qword 0x1b ; user SS
	push qword rax  ; user RSP
	mov rax, cr2 ; restore syscall number
	push r11     ; rflags is stored in r11
	push qword 0x23 ; user CS
	push qword rcx ; save return RIP
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

	; call logging function

	sti
	mov rdi, rax
	extern arch_syscall_log
	call arch_syscall_log ; compiled with __attribute__((no_caller_saved_registers)) 

	; prepare context argument
	mov rdi, rsp
	add rdi, 8 ; context pointer is after r9 argument

	cmp rax, syscallcount
	jae .nosyscall
	call [syscalltab + rax * 8]
	jmp .return
	.nosyscall:
	extern syscall_invalid
	call syscall_invalid
	.return:
	cli

	; call return logging function
	mov rdi, rax
	mov rsi, rdx
	extern arch_syscall_log_return
	call arch_syscall_log_return ; compiled with __attribute__((no_caller_saved_registers)) 

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
