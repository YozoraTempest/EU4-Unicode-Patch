EXTERN decode_z:PROC
EXTERN decode_layout_range:PROC
EXTERN g_alternate_end:QWORD
EXTERN copy_scalar:PROC
EXTERN prepare_wrap_context:PROC
EXTERN prepare_button_wrap:PROC
EXTERN unicode_wrap_before:PROC
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
EXTERN format_scalar:PROC
EXTERN reset_button_extra:PROC
EXTERN append_icon_tail:PROC
EXTERN g_main_measure_entry:QWORD
EXTERN g_main_format_return:QWORD
EXTERN g_main_plain_entry:QWORD
EXTERN g_main_icon_copy_return:QWORD
EXTERN g_main_icon_draw_return:QWORD
EXTERN g_button_format_return:QWORD
EXTERN g_button_plain_entry:QWORD
EXTERN g_button_icon_copy_return:QWORD
EXTERN g_button_icon_draw_return:QWORD
EXTERN g_button_draw_format_return:QWORD
EXTERN g_button_draw_plain_entry:QWORD
EXTERN g_bitmap_format_return:QWORD
EXTERN g_bitmap_plain_entry:QWORD
EXTERN g_bitmap_icon_end_return:QWORD
EXTERN dispatch_utf8:PROC
EXTERN g_input_return:QWORD
EXTERN trim_editor_grapheme:PROC
EXTERN g_editor_fit_return:QWORD
EXTERN bounded_text_length:PROC
EXTERN g_text_limit_return:QWORD
EXTERN mark_popup_font_glyph:PROC
EXTERN g_ui_vertices:QWORD
EXTERN g_main_page_return:QWORD
EXTERN g_button_page_return:QWORD
EXTERN begin_popup_font:PROC
EXTERN end_popup_font:PROC
EXTERN begin_main_paragraph:PROC
EXTERN begin_button_paragraph:PROC
EXTERN end_main_paragraph:PROC
EXTERN g_main_geometry_entry_return:QWORD
EXTERN g_main_geometry_end_return:QWORD
EXTERN g_button_geometry_entry_return:QWORD
EXTERN g_button_geometry_end_return:QWORD

include hook_context.inc

.CODE
main_geometry_entry_hook PROC
    SAVE_CONTEXT
    call begin_popup_font
    RESTORE_CONTEXT
    SAVE_CONTEXT
    mov r8, [rbp+2380h]
    lea r9, [rbp+2388h]
    call begin_main_paragraph
    mov [rsp+90h], rax
    RESTORE_CONTEXT
    mov r12, rdx
    mov r14, rcx
    jmp qword ptr [g_main_geometry_entry_return]
main_geometry_entry_hook ENDP

main_geometry_end_hook PROC
    SAVE_CONTEXT
    call end_main_paragraph
    call end_popup_font
    RESTORE_CONTEXT
    lea r11, [rsp+2408h]
    jmp qword ptr [g_main_geometry_end_return]
main_geometry_end_hook ENDP

button_geometry_entry_hook PROC
    SAVE_CONTEXT
    call begin_popup_font
    RESTORE_CONTEXT
    SAVE_CONTEXT
    mov rdx, [rbp+21c8h]
    lea r8, [rbp+21d8h]
    call begin_button_paragraph
    mov [rbp+21c8h], rax
    RESTORE_CONTEXT
    mov rbx, r8
    mov rdi, rcx
    jmp qword ptr [g_button_geometry_entry_return]
button_geometry_entry_hook ENDP

button_geometry_end_hook PROC
    SAVE_CONTEXT
    call end_main_paragraph
    call end_popup_font
    RESTORE_CONTEXT
    lea r11, [rsp+2260h]
    jmp qword ptr [g_button_geometry_end_return]
button_geometry_end_hook ENDP

main_page_hook PROC
    SAVE_CONTEXT
    mov rcx, [rbp+38h]
    movsxd rdx, dword ptr [rsp+138h]
    imul rdx, rdx, 1ch
    add rdx, qword ptr [g_ui_vertices]
    call mark_popup_font_glyph
    RESTORE_CONTEXT
    mov ebx, [rsp+48h]
    add ebx, 6
    jmp qword ptr [g_main_page_return]
main_page_hook ENDP

button_page_hook PROC
    SAVE_CONTEXT
    mov rcx, [rbp+58h]
    movsxd rdx, r14d
    imul rdx, rdx, 1ch
    add rdx, [rbp-18h]
    call mark_popup_font_glyph
    RESTORE_CONTEXT
    mov edx, [rbp+21c8h]
    jmp qword ptr [g_button_page_return]
button_page_hook ENDP

text_limit_hook PROC
    SAVE_CONTEXT
    mov rcx, rdi
    mov edx, r15d
    call bounded_text_length
    mov r15d, eax
    RESTORE_CONTEXT
    mov eax, 7d00h
    jmp qword ptr [g_text_limit_return]
text_limit_hook ENDP

input_hook PROC
    SAVE_CONTEXT
    mov rcx, r15
    mov rdx, r14
    lea r8, [rbp-44h]
    mov r9d, [rbp-3ch]
    call dispatch_utf8
    RESTORE_CONTEXT
    jmp qword ptr [g_input_return]
input_hook ENDP

editor_fit_hook PROC
    SAVE_CONTEXT
    mov rcx, rdi
    call trim_editor_grapheme
    RESTORE_CONTEXT
    jmp qword ptr [g_editor_fit_return]
editor_fit_hook ENDP

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
    test edi, edi
    jnz main_copy_context_ready
    mov rcx, [rsp+0a0h]
    mov rdx, [r12+10h]
    call prepare_wrap_context
main_copy_context_ready:
    mov rcx, [rsp+0a0h]
    mov rdx, [r12+10h]
    sub rdx, rdi
    mov r8, [rsp+0b0h]
    add r8, rsi
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
    cmp eax, 0ffh
    ja main_copy_plain
    jmp qword ptr [g_main_copy_return]
main_copy_plain:
    jmp qword ptr [g_main_measure_entry]
main_copy_hook ENDP

main_format_hook PROC
    lea r8, [rcx+r9]
    SAVE_CONTEXT
    mov rcx, r8
    call format_scalar
    cmp eax, 0ffh
    ja main_format_plain
    mov r10, rax
    shr r10, 32
    add r15d, r10d
    add [rsp+88h], r10
    add [rsp+98h], r10
    mov eax, eax
    mov [rsp+80h], rax
    RESTORE_CONTEXT
    cmp al, 0a7h
    jmp qword ptr [g_main_format_return]
main_format_plain:
    RESTORE_CONTEXT
    jmp qword ptr [g_main_plain_entry]
main_format_hook ENDP

main_icon_copy_hook PROC
    cmp byte ptr [r9], 0c2h
    jne main_icon_copy_end
    cmp byte ptr [r9+1], 0a3h
    jne main_icon_copy_end
    mov byte ptr [r8], 0a3h
    inc edi
    inc esi
main_icon_copy_end:
    mov byte ptr [rbp+rdx+1d0h], 0
    jmp qword ptr [g_main_icon_copy_return]
main_icon_copy_hook ENDP

main_icon_draw_hook PROC
    cmp byte ptr [r8], 0c2h
    jne main_icon_draw_end
    cmp byte ptr [r8+1], 0a3h
    jne main_icon_draw_end
    inc r15d
main_icon_draw_end:
    mov byte ptr [rbp+rcx+1d0h], 0
    jmp qword ptr [g_main_icon_draw_return]
main_icon_draw_hook ENDP

button_format_hook PROC
    lea rax, [rbp-38h]
    cmp r15, 10h
    cmovae rax, r12
    SAVE_CONTEXT
    lea rcx, [rax+rbx]
    call format_scalar
    cmp eax, 0ffh
    ja button_format_plain
    mov r10, rax
    shr r10, 32
    add r14d, r10d
    add rbx, r10
    test r10, r10
    jz button_format_single
    call reset_button_extra
button_format_single:
    RESTORE_CONTEXT
    cmp byte ptr [rbx+rax], 0a7h
    jmp qword ptr [g_button_format_return]
button_format_plain:
    RESTORE_CONTEXT
    jmp qword ptr [g_button_plain_entry]
button_format_hook ENDP

button_draw_format_hook PROC
    lea rax, [rbp-60h]
    cmp r12, 10h
    cmovae rax, r13
    mov ecx, r15d
    SAVE_CONTEXT
    add rcx, rax
    call format_scalar
    cmp eax, 0ffh
    ja button_draw_format_plain
    shr rax, 32
    add r15d, eax
    add [rsp+88h], rax
    RESTORE_CONTEXT
    cmp byte ptr [rax+rcx], 0a7h
    lea rax, [rbp-60h]
    jmp qword ptr [g_button_draw_format_return]
button_draw_format_plain:
    RESTORE_CONTEXT
    jmp qword ptr [g_button_draw_plain_entry]
button_draw_format_hook ENDP

bitmap_format_hook PROC
    mov r9, [rbx+18h]
    mov rcx, rbx
    cmp r9, 10h
    jb bitmap_format_inline
    mov rcx, [rbx]
bitmap_format_inline:
    SAVE_CONTEXT
    add rcx, rdi
    call format_scalar
    cmp eax, 0ffh
    ja bitmap_format_plain
    shr rax, 32
    add edi, eax
    RESTORE_CONTEXT
    cmp byte ptr [rdi+rcx], 0a7h
    jmp qword ptr [g_bitmap_format_return]
bitmap_format_plain:
    RESTORE_CONTEXT
    jmp qword ptr [g_bitmap_plain_entry]
bitmap_format_hook ENDP

bitmap_icon_end_hook PROC
    ; RDX still addresses the source string at this loop exit.
    cmp byte ptr [rdx+rdi], 0c2h
    jne bitmap_icon_end
    cmp byte ptr [rdx+rdi+1], 0a3h
    jne bitmap_icon_end
    inc edi
bitmap_icon_end:
    mov rax, [r14]
    lea rdx, [rsp+20h]
    mov byte ptr [rsp+rcx+20h], 0
    jmp qword ptr [g_bitmap_icon_end_return]
bitmap_icon_end_hook ENDP

button_icon_copy_hook PROC
    SAVE_CONTEXT
    lea rdx, [rbp-38h]
    cmp qword ptr [rbp-20h], 10h
    jb button_icon_copy_inline
    mov rdx, [rdx]
button_icon_copy_inline:
    add rdx, r14
    lea rcx, [rsp+168h]
    call append_icon_tail
    test al, al
    jz button_icon_copy_end
    inc r14d
button_icon_copy_end:
    RESTORE_CONTEXT
    mov byte ptr [rbp+rdx+0a0h], 0
    jmp qword ptr [g_button_icon_copy_return]
button_icon_copy_hook ENDP

button_icon_draw_hook PROC
    lea rax, [rbp-60h]
    cmp r12, 10h
    cmovae rax, r13
    cmp byte ptr [rax+r15], 0c2h
    jne button_icon_draw_end
    cmp byte ptr [rax+r15+1], 0a3h
    jne button_icon_draw_end
    inc r15d
button_icon_draw_end:
    mov byte ptr [rbp+rdx+0a0h], 0
    jmp qword ptr [g_button_icon_draw_return]
button_icon_draw_hook ENDP

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
    test r14d, r14d
    jnz button_copy_context_ready
    lea rcx, [rbp-38h]
    call prepare_button_wrap
button_copy_context_ready:
    mov rax, [rsp+80h]
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
    mov rdx, r12
    sub rdx, rbx
    call decode_layout_range
    cmp rax, -1
    je alternate_measure_end
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
alternate_measure_end:
    RESTORE_CONTEXT
    jmp qword ptr [g_alternate_end]
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
    mov ecx, edi
    call unicode_wrap_before
    test al, al
    jz wrap_unicode_blocked
    RESTORE_CONTEXT
    jmp wrap_allow
wrap_unicode_blocked:
    RESTORE_CONTEXT
    jmp qword ptr [g_wrap_branch]
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
