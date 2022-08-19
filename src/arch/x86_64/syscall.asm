section .rodata


extern syscall_libc_log
extern syscall_mmap
extern syscall_arch_ctl
extern syscall_gettid
extern syscall_open
extern syscall_read
func_count equ 6


func_table:
	dq syscall_libc_log
	dq syscall_mmap
	dq syscall_arch_ctl
	dq syscall_gettid
	dq syscall_open
	dq syscall_read
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

	push rcx ; old rip
	push r11 ; old rflags
	
	mov r11, 0x30 ; kernel data
	mov ds, r11
	mov es, r11

	; save C scratch registers
	; rax is not saved because it will be the return value
	; rdx is not saved because it will be the errno value

	push rdi
	push rsi
	push rcx
	push r8
	push r9
	push r10



	; system V C abi expects the third param to be in rcx
	; but the param is in rdx because of the syscall instruction

	mov rcx, r10


	call [func_table + rax * 8]

	; now restore the scratch registers

	pop r10
	pop r9
	pop r8
	pop rcx
	pop rsi
	pop rdi
	
	; restore entry state

	mov r11, 0x40 ; user data
	mov es,r11
	mov ds,r11

	pop r11
	pop rcx
	pop rsp
	swapgs

	o64 sysret
