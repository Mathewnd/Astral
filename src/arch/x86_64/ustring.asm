section .text:

global arch_u_strlen
global arch_u_strlen_cont

arch_u_strlen:
	xor rcx, rcx
	.loop:
		cmp byte [rdi],0
		je .done
		inc rcx
		inc rdi
		jmp .loop
	.done:
	mov [rsi], rcx
	xor rax, rax
	ret
arch_u_strlen_cont:
	mov rax, 1
	ret

global arch_u_memcpy
global arch_u_memcpy_cont

arch_u_memcpy:
	mov rcx, rdx
	rep movsb
	xor rax, rax
	ret
	arch_u_memcpy_cont:
	mov rax, 1
	ret

global arch_u_strcpy
global arch_u_strcpy_cont

arch_u_strcpy:
	.loop:
		mov al, [rsi]
		mov [rdi], al
		inc rsi
		inc rdi
		test al,al
		jz .done
		jmp .loop
	.done:
	xor rax,rax
	ret
	arch_u_strcpy_cont:
	mov rax, 1
	ret

