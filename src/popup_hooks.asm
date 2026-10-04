EXTERN copy_popup_scalar:PROC
EXTERN prepare_popup_wrap:PROC
EXTERN popup_wrap_after:PROC
EXTERN decode_z:PROC
EXTERN format_scalar:PROC
EXTERN next_layout_scalar:PROC
EXTERN begin_popup_font:PROC
EXTERN end_popup_font:PROC
EXTERN begin_popup_paragraph:PROC
EXTERN end_main_paragraph:PROC
EXTERN mark_popup_font_glyph:PROC
EXTERN g_popup_entry_return:QWORD
EXTERN g_popup_end_return:QWORD
EXTERN g_popup_data:QWORD
EXTERN g_popup_copy_return:QWORD
EXTERN g_popup_color_copy_return:QWORD
EXTERN g_popup_icon_copy_return:QWORD
EXTERN g_popup_format_return:QWORD
EXTERN g_popup_plain_entry:QWORD
EXTERN g_popup_measure_return:QWORD
EXTERN g_popup_wrap_return:QWORD
EXTERN g_popup_advance_entry:QWORD
EXTERN g_popup_advance_return:QWORD
EXTERN g_popup_draw_format_return:QWORD
EXTERN g_popup_draw_plain_entry:QWORD
EXTERN g_popup_draw_return:QWORD
EXTERN g_popup_icon_end_return:QWORD
EXTERN g_popup_measure_kern_return:QWORD
EXTERN g_popup_draw_kern_return:QWORD
EXTERN g_popup_page_return:QWORD
EXTERN g_map_kern_call:QWORD
include hook_context.inc

.CODE
popup_entry_hook PROC
    SAVE_CONTEXT
    mov rcx, r15
    call begin_popup_font
    RESTORE_CONTEXT
    SAVE_CONTEXT
    mov rcx, r15
    mov rdx, rsi
    mov r8d, [rbp+398h]
    call begin_popup_paragraph
    mov [rsp+90h], rax
    RESTORE_CONTEXT
    mov rsi, rdx
    mov rcx, rdx
    call qword ptr [g_popup_data]
    jmp qword ptr [g_popup_entry_return]
popup_entry_hook ENDP

popup_end_hook PROC
    SAVE_CONTEXT
    call end_main_paragraph
    call end_popup_font
    RESTORE_CONTEXT
    lea r11, [rsp+438h]
    jmp qword ptr [g_popup_end_return]
popup_end_hook ENDP

popup_copy_hook PROC
    SAVE_CONTEXT
    test edi, edi
    jnz popup_copy_ready
    mov rcx, rsi
    call prepare_popup_wrap
popup_copy_ready:
    mov rdx, [rsp+80h]
    add rdx, rdi
    lea rcx, [rbp+50h]
    call copy_popup_scalar
    RESTORE_CONTEXT
    jmp qword ptr [g_popup_copy_return]
popup_copy_hook ENDP

popup_color_copy_hook PROC
    SAVE_CONTEXT
    lea rdx, [rax+rdi]
    lea rcx, [rbp+50h]
    call copy_popup_scalar
    shr rax, 32
    add edi, eax
    RESTORE_CONTEXT
    jmp qword ptr [g_popup_color_copy_return]
popup_color_copy_hook ENDP

popup_icon_copy_hook PROC
    SAVE_CONTEXT
    lea rdx, [rax+rdi]
    lea rcx, [rbp+50h]
    call copy_popup_scalar
    mov ebx, eax
    shr rax, 32
    add edi, eax
    RESTORE_CONTEXT
    jmp qword ptr [g_popup_icon_copy_return]
popup_icon_copy_hook ENDP

popup_format_hook PROC
    mov rax, rsi
    cmp qword ptr [rsi+18h], 10h
    jb popup_format_inline
    mov rax, [rsi]
popup_format_inline:
    SAVE_CONTEXT
    lea rcx, [rax+rdi]
    call format_scalar
    cmp eax, 0ffh
    ja popup_format_plain
    shr rax, 32
    add edi, eax
    RESTORE_CONTEXT
    mov rcx, rsi
    cmp byte ptr [rax+rdi], 0a7h
    jmp qword ptr [g_popup_format_return]
popup_format_plain:
    RESTORE_CONTEXT
    jmp qword ptr [g_popup_plain_entry]
popup_format_hook ENDP

popup_measure_hook PROC
    SAVE_CONTEXT
    lea rcx, [rax+rdi]
    call decode_z
    mov r10, rax
    shr r10, 32
    add edi, r10d
    mov eax, eax
    mov [rsp+80h], rax
    RESTORE_CONTEXT
    LOOKUP_GLYPH r12, r15, rax, 120h
    test r12, r12
    jmp qword ptr [g_popup_measure_return]
popup_measure_hook ENDP

popup_wrap_hook PROC
    cmp word ptr [r12+6], 0
    je popup_wrap_allowed
    SAVE_CONTEXT
    mov ecx, edi
    call popup_wrap_after
    test al, al
    jz popup_wrap_blocked
    RESTORE_CONTEXT
popup_wrap_allowed:
    jmp qword ptr [g_popup_wrap_return]
popup_wrap_blocked:
    RESTORE_CONTEXT
    jmp qword ptr [g_popup_advance_entry]
popup_wrap_hook ENDP

popup_advance_hook PROC
    SAVE_CONTEXT
    mov rcx, rsi
    mov edx, edi
    call next_layout_scalar
    mov edi, eax
    RESTORE_CONTEXT
    cmp edi, [rsi+10h]
    jmp qword ptr [g_popup_advance_return]
popup_advance_hook ENDP

popup_draw_format_hook PROC
    lea rax, [rsp+60h]
    cmp r8, 10h
    cmovae rax, r9
    SAVE_CONTEXT
    lea rcx, [rax+rsi]
    call format_scalar
    cmp eax, 0ffh
    ja popup_draw_format_plain
    shr rax, 32
    add esi, eax
    RESTORE_CONTEXT
    cmp byte ptr [rax+rsi], 0a7h
    lea rax, [rsp+60h]
    jmp qword ptr [g_popup_draw_format_return]
popup_draw_format_plain:
    RESTORE_CONTEXT
    jmp qword ptr [g_popup_draw_plain_entry]
popup_draw_format_hook ENDP

popup_draw_hook PROC
    SAVE_CONTEXT
    lea rcx, [rax+rsi]
    call decode_z
    mov r10, rax
    shr r10, 32
    add esi, r10d
    mov eax, eax
    mov [rsp+80h], rax
    RESTORE_CONTEXT
    LOOKUP_GLYPH r13, r15, rax, 120h
    test r13, r13
    jmp qword ptr [g_popup_draw_return]
popup_draw_hook ENDP

popup_icon_end_hook PROC
    lea rax, [rsp+60h]
    cmp r8, 10h
    cmovae rax, r9
    cmp byte ptr [rax+rsi], 0c2h
    jne popup_icon_end
    cmp byte ptr [rax+rsi+1], 0a3h
    jne popup_icon_end
    inc esi
popup_icon_end:
    mov byte ptr [rbp+rdx-70h], 0
    jmp qword ptr [g_popup_icon_end_return]
popup_icon_end_hook ENDP

popup_measure_kern_hook PROC
    cmp edx, 80h
    jae popup_measure_kern_none
    cmp r8d, 80h
    jae popup_measure_kern_none
    call qword ptr [g_map_kern_call]
    jmp popup_measure_kern_done
popup_measure_kern_none:
    xorps xmm0, xmm0
popup_measure_kern_done:
    addss xmm6, xmm0
    addss xmm7, xmm0
    jmp qword ptr [g_popup_measure_kern_return]
popup_measure_kern_hook ENDP

popup_draw_kern_hook PROC
    cmp edx, 80h
    jae popup_draw_kern_none
    cmp r8d, 80h
    jae popup_draw_kern_none
    call qword ptr [g_map_kern_call]
    jmp popup_draw_kern_done
popup_draw_kern_none:
    xorps xmm0, xmm0
popup_draw_kern_done:
    addss xmm13, xmm0
    jmp qword ptr [g_popup_draw_kern_return]
popup_draw_kern_hook ENDP

popup_page_hook PROC
    SAVE_CONTEXT
    mov rcx, r13
    mov rdx, [rbp-80h]
    sub rdx, 34h
    call mark_popup_font_glyph
    RESTORE_CONTEXT
    mov ecx, [rbp+388h]
    add ecx, 6
    jmp qword ptr [g_popup_page_return]
popup_page_hook ENDP
END
