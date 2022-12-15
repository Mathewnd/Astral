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
