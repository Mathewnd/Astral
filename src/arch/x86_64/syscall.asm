section .rodata


extern syscall_libc_log
extern syscall_mmap
extern syscall_arch_ctl
extern syscall_gettid
extern syscall_open
extern syscall_read
extern syscall_lseek
extern syscall_close
extern syscall_isatty
extern syscall_write
extern syscall_stat
extern syscall_fstat
extern syscall_fork
extern syscall_execve
extern syscall_waitpid
extern syscall_exit
extern syscall_dup
extern syscall_dup2
extern syscall_fcntl
extern syscall_getpid
extern syscall_getdirent
extern syscall_ioctl
extern syscall_chdir
extern syscall_fstatat
extern syscall_pipe2
extern syscall_mkdir
extern syscall_munmap
extern syscall_umask
extern syscall_poll
extern syscall_fchmodat
extern syscall_openat
extern syscall_chroot
extern syscall_mkdirat
func_count equ 33


func_table:
	dq syscall_libc_log
	dq syscall_mmap
	dq syscall_arch_ctl
	dq syscall_gettid
	dq syscall_open
	dq syscall_read
	dq syscall_lseek
	dq syscall_close
	dq syscall_isatty
	dq syscall_write
	dq syscall_stat
	dq syscall_fstat
	dq syscall_fork
	dq syscall_execve
	dq syscall_waitpid
	dq syscall_exit
	dq syscall_dup
	dq syscall_dup2
	dq syscall_fcntl
	dq syscall_getpid
	dq syscall_getdirent
	dq syscall_ioctl
	dq syscall_chdir
	dq syscall_fstatat
	dq syscall_pipe2
	dq syscall_mkdir
	dq syscall_munmap
	dq syscall_umask
	dq syscall_poll
	dq syscall_fchmodat
	dq syscall_openat
	dq syscall_chroot
	dq syscall_mkdirat
section .text
global asm_syscall_entry

; the registers for the system call are:
; RETURN:
; rax -> ret value
; rdx -> errno
; PARAM:
; rax -> func
; rdi, rsi, rdx, r10, r8, r9 -> parameters
; clobbers: rax, rdx, rcx, r11

asm_syscall_entry:
	swapgs

	xchg rsp, [gs:0] ; get new stack
	push qword [gs:0] ; save old stack
	mov [gs:0], rsp ; return back the kernel stack
	add qword [gs:0], 8 ; fix it
	
	sti
	
	; assemble and save context needed for some system calls	

	push qword 0x43 ; ss
	push qword [rsp+8] ; rsp
	push r11 ; old rflags	
	push qword 0x3b ; cs
	push rcx ; old rip
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
	push qword 0x43 ; ds
	push qword 0x43 ; es
	push qword 0 ; fs
	push qword 0 ; gs
	push qword 0 ; cr2

	mov r11, 0x30 ; kernel data
	mov ds, r11
	mov es, r11

	; system V C abi expects the third param to be in rcx
	; but the param is in rdx because of the syscall instruction

	mov rcx, r10

	; set up context for calls that need it

	cmp rax, 12 ; fork
	
	jne .not_fork

	mov rdi, rsp

	jmp .do_syscall
	
	.not_fork:

	.do_syscall:

	call [func_table + rax * 8]

	; restore entry state

	mov r11, 0x43 ; user data
	mov es,r11
	mov ds,r11

	add rsp, 0x30 ; cr2 gs fs es ds rax
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
	add rsp, 0x30 ; err, rip, cs, rflags, rsp, ss	
	pop rsp ; actual rsp

	cli


	swapgs

	o64 sysret
