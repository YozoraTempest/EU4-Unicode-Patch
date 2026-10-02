EXTERN decode_z:PROC
EXTERN copy_scalar:PROC
EXTERN previous_slot:PROC
EXTERN g_main_draw_return:QWORD
EXTERN g_main_copy_return:QWORD
EXTERN g_main_measure_return:QWORD
EXTERN g_bitmap_measure_return:QWORD
EXTERN g_bitmap_split_return:QWORD
EXTERN g_copy_buffer:QWORD
EXTERN g_heap_pointer:QWORD
EXTERN g_heap_alloc:QWORD
EXTERN g_heap_return:QWORD
EXTERN construct_scalar:PROC
EXTERN previous_extra:PROC
EXTERN g_button_copy_return:QWORD
EXTERN g_button_measure_return:QWORD
EXTERN g_button_draw_return:QWORD
EXTERN g_button_loop:QWORD
EXTERN g_button_end:QWORD
EXTERN g_alternate_measure_return:QWORD
EXTERN g_wrap_return:QWORD
EXTERN g_wrap_branch:QWORD

; Each hook enters at an instruction boundary with RSP 16-byte aligned.
; Save all volatile GPRs, flags and SIMD registers before calling C++.
SAVE_CONTEXT MACRO
    pushfq
    sub rsp, 0e8h
    movdqu [rsp+20h], xmm0
    movdqu [rsp+30h], xmm1
    movdqu [rsp+40h], xmm2
    movdqu [rsp+50h], xmm3
    movdqu [rsp+60h], xmm4
    movdqu [rsp+70h], xmm5
    mov [rsp+80h], rax
    mov [rsp+88h], rcx
    mov [rsp+90h], rdx
    mov [rsp+98h], r8
    mov [rsp+0a0h], r9
    mov [rsp+0a8h], r10
    mov [rsp+0b0h], r11
ENDM
RESTORE_CONTEXT MACRO
    movdqu xmm0, [rsp+20h]
    movdqu xmm1, [rsp+30h]
    movdqu xmm2, [rsp+40h]
    movdqu xmm3, [rsp+50h]
    movdqu xmm4, [rsp+60h]
    movdqu xmm5, [rsp+70h]
    mov rax, [rsp+80h]
    mov rcx, [rsp+88h]
    mov rdx, [rsp+90h]
    mov r8, [rsp+98h]
    mov r9, [rsp+0a0h]
    mov r10, [rsp+0a8h]
    mov r11, [rsp+0b0h]
    add rsp, 0e8h
    popfq
ENDM
LOOKUP_GLYPH MACRO result, font, slot, table_offset
    LOCAL selected
    mov result, [font+slot*8+table_offset]
    test result, result
    jnz selected
    cmp slot, 20h
    jb selected
    mov result, [font+10130h+table_offset]
    test result, result
    jnz selected
    mov result, [font+1f8h+table_offset]
selected:
ENDM

.CODE
main_draw_hook PROC
    SAVE_CONTEXT
    add rcx, r9
    call decode_z
    mov r10, rax
    shr r10, 32
    add r15d, r10d
    mov eax, eax
    mov [rsp+98h], rax
    RESTORE_CONTEXT
    movss xmm3, dword ptr [r14+968h]
    LOOKUP_GLYPH rdx, r14, r8, 120h
    jmp qword ptr [g_main_draw_return]
main_draw_hook ENDP

main_copy_hook PROC
    movsxd r9, edi
    mov rdx, [rbp-8]
    add r9, rdx
    movsxd rcx, esi
    mov r11, g_copy_buffer
    SAVE_CONTEXT
    mov rcx, r9
    mov rdx, [r12+10h]
    sub rdx, rdi
    lea r8, [r11+rsi]
    mov r9d, 7d00h
    sub r9d, esi
    call copy_scalar
    mov r10, rax
    shr r10, 32
    add edi, r10d
    add esi, r10d
    inc esi
    add [rsp+0a0h], r10
    mov eax, eax
    mov [rsp+80h], rax
    RESTORE_CONTEXT
    jmp qword ptr [g_main_copy_return]
main_copy_hook ENDP

main_measure_hook PROC
    SAVE_CONTEXT
    call previous_slot
    mov [rsp+80h], rax
    RESTORE_CONTEXT
    LOOKUP_GLYPH rcx, r14, rax, 120h
    mov [rbp], rcx
    test rcx, rcx
    jmp qword ptr [g_main_measure_return]
main_measure_hook ENDP

bitmap_measure_hook PROC
    SAVE_CONTEXT
    lea rcx, [rdi+rax]
    call decode_z
    mov r10, rax
    shr r10, 32
    add edi, r10d
    test r10d, r10d
    jz bitmap_measure_single
    xorps xmm6, xmm6
bitmap_measure_single:
    mov eax, eax
    mov [rsp+80h], rax
    RESTORE_CONTEXT
    LOOKUP_GLYPH rcx, r14, rax, 120h
    test rcx, rcx
    jmp qword ptr [g_bitmap_measure_return]
bitmap_measure_hook ENDP

bitmap_split_hook PROC
    SAVE_CONTEXT
    lea rcx, [rdx+rax]
    call decode_z
    mov r10, rax
    shr r10, 32
    add edi, r10d
    mov eax, eax
    mov [rsp+80h], rax
    RESTORE_CONTEXT
    movss xmm6, dword ptr [r14+848h]
    LOOKUP_GLYPH r15, r14, rax, 0
    test r15, r15
    jmp qword ptr [g_bitmap_split_return]
bitmap_split_hook ENDP

button_copy_hook PROC
    SAVE_CONTEXT
    lea rdx, [rax+rbx]
    lea rcx, [rsp+138h]
    call construct_scalar
    mov [rsp+80h], rax
    RESTORE_CONTEXT
    jmp qword ptr [g_button_copy_return]
button_copy_hook ENDP

button_measure_hook PROC
    mov r9d, r14d
    SAVE_CONTEXT
    lea rcx, [r9+rax]
    call decode_z
    mov r10, rax
    shr r10, 32
    add [rsp+0a0h], r10
    mov eax, eax
    mov [rsp+80h], rax
    RESTORE_CONTEXT
    LOOKUP_GLYPH r11, rcx, rax, 0
    test r11, r11
    jmp qword ptr [g_button_measure_return]
button_measure_hook ENDP

button_advance_hook PROC
    SAVE_CONTEXT
    call previous_extra
    add r14d, eax
    inc r14d
    RESTORE_CONTEXT
    cmp r14d, [rbp-28h]
    jl button_next
    mov r15d, [rbp+21c8h]
    jmp qword ptr [g_button_end]
button_next:
    jmp qword ptr [g_button_loop]
button_advance_hook ENDP

button_draw_hook PROC
    mov ecx, r15d
    SAVE_CONTEXT
    add rcx, rax
    call decode_z
    mov r10, rax
    shr r10, 32
    add r15d, r10d
    mov eax, eax
    mov [rsp+80h], rax
    RESTORE_CONTEXT
    movss xmm11, dword ptr [rdx+848h]
    LOOKUP_GLYPH r8, rdx, rax, 0
    mov [rbp+58h], r8
    jmp qword ptr [g_button_draw_return]
button_draw_hook ENDP

alternate_measure_hook PROC
    SAVE_CONTEXT
    lea rcx, [rbx+rbp]
    call decode_z
    mov r10, rax
    shr r10, 32
    add rbx, r10
    add edi, r10d
    mov eax, eax
    mov [rsp+90h], rax
    RESTORE_CONTEXT
    lea rcx, [r15+120h]
    LOOKUP_GLYPH r11, rcx, rdx, 0
    test r11, r11
    jmp qword ptr [g_alternate_measure_return]
alternate_measure_hook ENDP

main_wrap_hook PROC
    SAVE_CONTEXT
    call previous_slot
    cmp eax, 0ffh
    ja wrap_unicode
    RESTORE_CONTEXT
    cmp word ptr [rcx+6], 0
    jne wrap_original
    jmp wrap_allow
wrap_unicode:
    RESTORE_CONTEXT
wrap_allow:
    lea eax, [rbx+rbx]
    movd xmm1, eax
    jmp qword ptr [g_wrap_return]
wrap_original:
    jmp qword ptr [g_wrap_branch]
main_wrap_hook ENDP

heap_zero_hook PROC
    mov rcx, g_heap_pointer
    mov rcx, [rcx]
    mov r8, rbx
    mov edx, 8
    mov rax, g_heap_alloc
    call qword ptr [rax]
    test rax, rax
    jmp qword ptr [g_heap_return]
heap_zero_hook ENDP
END
