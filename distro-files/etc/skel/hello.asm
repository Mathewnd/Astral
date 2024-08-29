section .text
global _start
_start: 
	mov rax, 7
	mov rdi, 1
	mov rsi, msg
	mov rdx, 14
	syscall
	mov rax, 13
	xor rdi, rdi
	syscall
section .data
msg: db "Hello, world!", 0xA
