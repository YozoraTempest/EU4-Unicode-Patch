EXTERN decode_z:PROC
EXTERN copy_scalar:PROC
EXTERN construct_map_scalar:PROC
EXTERN map_scalar_size:PROC
EXTERN map_scalar_gaps:PROC
EXTERN g_map_copy_return:QWORD
EXTERN g_map_measure_return:QWORD
EXTERN g_map_draw_return:QWORD
EXTERN g_map_kern_return:QWORD
EXTERN g_map_kern_call:QWORD
EXTERN g_map_justify_draw_return:QWORD
EXTERN g_map_justify_measure_return:QWORD
EXTERN g_map_justify_advance_return:QWORD
EXTERN g_map_adjust_copy_return:QWORD
EXTERN g_map_adjust_glyph_return:QWORD
EXTERN g_map_upper_return:QWORD
EXTERN g_map_lower_return:QWORD
include hook_context.inc

.CODE
map_copy_hook PROC
    SAVE_CONTEXT
    lea rdx, [r15+rax]
    lea rcx, [rsp+148h]
    call construct_map_scalar
    mov [rsp+80h], rax
    RESTORE_CONTEXT
    jmp qword ptr [g_map_copy_return]
map_copy_hook ENDP

map_measure_hook PROC
    SAVE_CONTEXT
    lea rcx, [r15+rax]
    call decode_z
    mov r10, rax
    shr r10, 32
    add r14d, r10d
    add r15, r10
    mov eax, eax
    mov [rsp+80h], rax
    RESTORE_CONTEXT
    LOOKUP_GLYPH r11, r9, rax, 120h
    test r11, r11
    jmp qword ptr [g_map_measure_return]
map_measure_hook ENDP

map_draw_hook PROC
    movss dword ptr [rbp-60h], xmm3
    SAVE_CONTEXT
    lea rcx, [r9+rax]
    call decode_z
    mov r10, rax
    shr r10, 32
    add [rsp+0b0h], r10
    add [rsp+0a0h], r10
    mov eax, eax
    mov [rsp+80h], rax
    RESTORE_CONTEXT
    LOOKUP_GLYPH rdx, r15, rax, 0
    test rdx, rdx
    jmp qword ptr [g_map_draw_return]
map_draw_hook ENDP

map_kern_hook PROC
    lea rcx, [rsp+78h]
    cmp rbx, 10h
    cmovae rcx, rsi
    movzx r8d, byte ptr [rax+rcx]
    movzx edx, byte ptr [rcx+r9]
    ; The native kerning routine indexes two byte-sized character IDs.
    ; Its tables contain ASCII pairs; never index them with UTF-8 tail bytes.
    cmp r8d, 80h
    jae map_kern_none
    cmp edx, 80h
    jae map_kern_none
    mov rcx, r15
    call qword ptr [g_map_kern_call]
    jmp qword ptr [g_map_kern_return]
map_kern_none:
    xorps xmm0, xmm0
    jmp qword ptr [g_map_kern_return]
map_kern_hook ENDP

map_justify_draw_hook PROC
    SAVE_CONTEXT
    add rcx, rax
    call decode_z
    mov eax, eax
    mov [rsp+80h], rax
    RESTORE_CONTEXT
    mov byte ptr [rbp+808h], 0
    cmp eax, 0ffh
    ja map_justify_marker_done
    mov byte ptr [rbp+808h], al
map_justify_marker_done:
    movss xmm12, dword ptr [rdx+848h]
    LOOKUP_GLYPH r14, rdx, rax, 0
    test r14, r14
    jmp qword ptr [g_map_justify_draw_return]
map_justify_draw_hook ENDP

map_justify_measure_hook PROC
    SAVE_CONTEXT
    mov rcx, rdi
    call map_scalar_gaps
    mov [rsp+80h], rax
    RESTORE_CONTEXT
    movd xmm6, esi
    cvtdq2ps xmm6, xmm6
    movd xmm1, eax
    jmp qword ptr [g_map_justify_measure_return]
map_justify_measure_hook ENDP

map_justify_advance_hook PROC
    SAVE_CONTEXT
    mov rdx, rcx
    mov rcx, rdi
    call map_scalar_size
    add [rsp+88h], rax
    RESTORE_CONTEXT
    inc esi
    mov dword ptr [rbp+7e8h], esi
    mov qword ptr [rbp+118h], rcx
    jmp qword ptr [g_map_justify_advance_return]
map_justify_advance_hook ENDP

map_adjust_copy_hook PROC
    lea rax, [rbp+90h]
    cmp r13, 10h
    cmovae rax, rsi
    SAVE_CONTEXT
    lea rcx, [rax+rbx]
    mov rdx, [rbp+0a0h]
    sub rdx, rbx
    mov r8, rbp
    mov r9d, 4
    call copy_scalar
    shr rax, 32
    add rbx, rax
    inc rax
    mov [rsp+98h], rax
    RESTORE_CONTEXT
    jmp qword ptr [g_map_adjust_copy_return]
map_adjust_copy_hook ENDP

map_adjust_glyph_hook PROC
    lea rax, [rbp+90h]
    cmp r13, 10h
    cmovae rax, rsi
    SAVE_CONTEXT
    add rcx, rax
    call decode_z
    mov r10, rax
    shr r10, 32
    add [rsp+88h], r10
    mov eax, eax
    mov [rsp+80h], rax
    RESTORE_CONTEXT
    LOOKUP_GLYPH rdx, r14, rax, 0
    jmp qword ptr [g_map_adjust_glyph_return]
map_adjust_glyph_hook ENDP

map_upper_hook PROC
    lea rbx, [rax+rbp]
    movzx eax, byte ptr [rbx]
    cmp eax, 'a'
    jb map_upper_done
    cmp eax, 'z'
    ja map_upper_done
    sub eax, 20h
    mov [rbx], al
map_upper_done:
    inc edi
    mov eax, edi
    jmp qword ptr [g_map_upper_return]
map_upper_hook ENDP

map_lower_hook PROC
    lea rbx, [rax+rbp]
    movzx eax, byte ptr [rbx]
    cmp eax, 'A'
    jb map_lower_done
    cmp eax, 'Z'
    ja map_lower_done
    add eax, 20h
    mov [rbx], al
map_lower_done:
    inc edi
    mov eax, edi
    jmp qword ptr [g_map_lower_return]
map_lower_hook ENDP
END
