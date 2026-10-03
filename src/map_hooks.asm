EXTERN decode_z:PROC
EXTERN copy_scalar:PROC
EXTERN construct_map_scalar:PROC
EXTERN map_scalar_size:PROC
EXTERN map_scalar_count:PROC
EXTERN format_scalar:PROC
EXTERN map_last_scalar_offset:PROC
EXTERN copy_last_map_scalar:PROC
EXTERN mark_map_font_glyph:PROC
EXTERN remember_map_font_glyph:PROC
EXTERN mark_current_map_font_glyph:PROC
EXTERN g_map_copy_return:QWORD
EXTERN g_map_measure_return:QWORD
EXTERN g_map_draw_return:QWORD
EXTERN g_map_kern_return:QWORD
EXTERN g_map_kern_call:QWORD
EXTERN g_map_justify_draw_return:QWORD
EXTERN g_map_justify_measure_return:QWORD
EXTERN g_map_justify_count_return:QWORD
EXTERN g_map_justify_single_return:QWORD
EXTERN g_map_justify_advance_return:QWORD
EXTERN g_map_adjust_copy_return:QWORD
EXTERN g_map_adjust_glyph_return:QWORD
EXTERN g_map_vertex_count_return:QWORD
EXTERN g_map_upper_return:QWORD
EXTERN g_map_lower_return:QWORD
EXTERN g_map_page_tag_return:QWORD
EXTERN g_map_justify_page_tag_return:QWORD
EXTERN g_map_fit_format_return:QWORD
EXTERN g_map_fit_plain_entry:QWORD
EXTERN g_map_fit_measure_return:QWORD
EXTERN g_map_fit_kern_return:QWORD
EXTERN g_map_fit_icon_end_return:QWORD
EXTERN g_map_adjust_gap_end_return:QWORD
EXTERN g_map_adjust_last_return:QWORD
include hook_context.inc

.CODE
map_fit_format_hook PROC
    mov rax, rbx
    mov r8, [rbx+18h]
    cmp r8, 10h
    jb map_fit_format_inline
    mov rax, [rbx]
map_fit_format_inline:
    mov edx, edi
    SAVE_CONTEXT
    lea rcx, [rax+rdx]
    call format_scalar
    cmp eax, 0ffh
    ja map_fit_format_plain
    shr rax, 32
    add edi, eax
    RESTORE_CONTEXT
    mov edx, edi
    jmp qword ptr [g_map_fit_format_return]
map_fit_format_plain:
    RESTORE_CONTEXT
    jmp qword ptr [g_map_fit_plain_entry]
map_fit_format_hook ENDP

map_fit_measure_hook PROC
    SAVE_CONTEXT
    lea rcx, [rax+rdx]
    call decode_z
    mov r10, rax
    shr r10, 32
    add edi, r10d
    add [rsp+90h], r10
    mov eax, eax
    mov [rsp+80h], rax
    RESTORE_CONTEXT
    LOOKUP_GLYPH r11, r13, rax, 120h
    movss xmm1, dword ptr [r13+968h]
    test r11, r11
    jmp qword ptr [g_map_fit_measure_return]
map_fit_measure_hook ENDP

map_fit_kern_hook PROC
    cmp edx, 80h
    jae map_fit_kern_none
    cmp r8d, 80h
    jae map_fit_kern_none
    call qword ptr [g_map_kern_call]
    jmp map_fit_kern_done
map_fit_kern_none:
    xorps xmm0, xmm0
map_fit_kern_done:
    addss xmm6, xmm0
    addss xmm7, xmm0
    mov ecx, dword ptr [rbp+1070h]
    jmp qword ptr [g_map_fit_kern_return]
map_fit_kern_hook ENDP

map_fit_icon_end_hook PROC
    mov rax, rbx
    cmp qword ptr [rbx+18h], 10h
    jb map_fit_icon_end_inline
    mov rax, [rbx]
map_fit_icon_end_inline:
    cmp byte ptr [rax+rdi], 0c2h
    jne map_fit_icon_end_done
    cmp byte ptr [rax+rdi+1], 0a3h
    jne map_fit_icon_end_done
    inc edi
map_fit_icon_end_done:
    mov byte ptr [rsp+rcx+40h], r12b
    mov rax, [r13]
    lea rdx, [rsp+40h]
    mov rcx, r13
    jmp qword ptr [g_map_fit_icon_end_return]
map_fit_icon_end_hook ENDP

map_adjust_gap_end_hook PROC
    SAVE_CONTEXT
    lea rcx, [rbp+90h]
    call map_last_scalar_offset
    mov [rsp+80h], rax
    RESTORE_CONTEXT
    mov r12, rax
    jmp qword ptr [g_map_adjust_gap_end_return]
map_adjust_gap_end_hook ENDP

map_adjust_last_hook PROC
    SAVE_CONTEXT
    lea rcx, [rbp+90h]
    lea rdx, [rbp]
    call copy_last_map_scalar
    mov [rsp+98h], rax
    RESTORE_CONTEXT
    jmp qword ptr [g_map_adjust_last_return]
map_adjust_last_hook ENDP

map_vertex_count_hook PROC
    lea rax, [rbx+10h]
    cmp r9, 10h
    jb map_vertex_count_inline
    mov rax, [rbx+10h]
map_vertex_count_inline:
    SAVE_CONTEXT
    add rcx, rax
    call decode_z
    mov r10, rax
    shr r10, 32
    add [rsp+88h], r10
    mov eax, eax
    mov [rsp+80h], rax
    RESTORE_CONTEXT
    LOOKUP_GLYPH rdx, r12, rax, 120h
    jmp qword ptr [g_map_vertex_count_return]
map_vertex_count_hook ENDP

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

map_page_tag_hook PROC
    SAVE_CONTEXT
    mov rcx, rdx
    movsxd rax, edi
    lea rax, [rax+rax*4]
    lea rdx, [r10+rax*4]
    call mark_map_font_glyph
    RESTORE_CONTEXT
    add edi, 6
    movsx eax, word ptr [rdx+0ch]
    movd xmm0, eax
    cvtdq2ps xmm0, xmm0
    jmp qword ptr [g_map_page_tag_return]
map_page_tag_hook ENDP

map_justify_page_tag_hook PROC
    SAVE_CONTEXT
    movsxd rax, r13d
    lea rax, [rax+rax*4]
    lea rcx, [r12+rax*4]
    call mark_current_map_font_glyph
    RESTORE_CONTEXT
    add r13d, 6
    mov rcx, [rbp+118h]
    mov rdi, [rbp+7c8h]
    jmp qword ptr [g_map_justify_page_tag_return]
map_justify_page_tag_hook ENDP

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
    SAVE_CONTEXT
    mov rcx, r14
    call remember_map_font_glyph
    RESTORE_CONTEXT
    test r14, r14
    jmp qword ptr [g_map_justify_draw_return]
map_justify_draw_hook ENDP

map_justify_count_hook PROC
    SAVE_CONTEXT
    mov rcx, rdi
    call map_scalar_count
    mov [rsp+80h], rax
    RESTORE_CONTEXT
    ; Layout uses scalar count; the iterator still needs the byte length.
    mov rcx, [rdi+10h]
    mov [rbp+168h], rax
    lea eax, [rax-2]
    jmp qword ptr [g_map_justify_count_return]
map_justify_count_hook ENDP

map_justify_measure_hook PROC
    cmp qword ptr [rbp+168h], 1
    jbe map_justify_single
    mov eax, dword ptr [rbp+168h]
    dec eax
    movd xmm6, esi
    cvtdq2ps xmm6, xmm6
    movd xmm1, eax
    jmp qword ptr [g_map_justify_measure_return]
map_justify_single:
    ; Preserve the native single-character spacing initialized to 1.
    jmp qword ptr [g_map_justify_single_return]
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
