; segment is in rdi
; returns 1 for success or 0 for failure
load_seg_gs:
global load_seg_gs
	xor rax, rax
	mov gs, rdi
	inc rax
	ret
load_seg_gs_fallback:
global load_seg_gs_fallback
	ret

; segment is in rdi
; returns 1 for success or 0 for failure
load_seg_fs:
global load_seg_fs
	xor rax, rax
	mov fs, rdi
	inc rax
	ret
load_seg_fs_fallback:
global load_seg_fs_fallback
	ret
