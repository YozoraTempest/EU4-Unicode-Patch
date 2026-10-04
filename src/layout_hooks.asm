EXTERN decode_z:PROC
EXTERN format_scalar:PROC
EXTERN format_layout_range:PROC
EXTERN next_layout_scalar:PROC
EXTERN next_layout_offset:PROC
EXTERN g_bitmap_advance_return:QWORD
EXTERN g_list_measure_return:QWORD
EXTERN g_list_advance_return:QWORD
EXTERN g_split_format_return:QWORD
EXTERN g_split_plain_entry:QWORD
EXTERN g_list_format_return:QWORD
EXTERN g_list_plain_entry:QWORD
EXTERN g_alternate_format_return:QWORD
EXTERN g_alternate_plain_entry:QWORD
EXTERN g_alternate_end:QWORD
EXTERN g_alternate_advance_return:QWORD
EXTERN g_split_kern_return:QWORD
EXTERN g_list_kern_return:QWORD
EXTERN g_alternate_kern_return:QWORD
EXTERN g_map_kern_call:QWORD
include hook_context.inc

.CODE
bitmap_advance_hook PROC
    SAVE_CONTEXT
    mov rcx, rbx
    mov edx, edi
    call next_layout_scalar
    mov edi, eax
    RESTORE_CONTEXT
    mov edx, edi
    mov r10d, [rbx+10h]
    cmp edi, r10d
    jmp qword ptr [g_bitmap_advance_return]
bitmap_advance_hook ENDP

list_measure_hook PROC
    SAVE_CONTEXT
    lea rcx, [rax+rdx]
    call decode_z
    mov eax, eax
    mov [rsp+80h], rax
    RESTORE_CONTEXT
    mov r8, [rbp-48h]
    movss xmm6, dword ptr [r8+848h]
    LOOKUP_GLYPH r12, r8, rax, 0
    test r12, r12
    jmp qword ptr [g_list_measure_return]
list_measure_hook ENDP

list_advance_hook PROC
    SAVE_CONTEXT
    mov rcx, rdi
    mov edx, ebx
    call next_layout_scalar
    mov ebx, eax
    RESTORE_CONTEXT
    mov esi, ebx
    mov ecx, [rdi+10h]
    movzx r9d, byte ptr [rbp+148h]
    jmp qword ptr [g_list_advance_return]
list_advance_hook ENDP

split_format_hook PROC
    mov rcx, rbx
    mov r9, [rbx+18h]
    cmp r9, 10h
    jb split_format_inline
    mov rcx, [rbx]
split_format_inline:
    SAVE_CONTEXT
    add rcx, rdx
    call format_scalar
    cmp eax, 0ffh
    ja split_format_plain
    shr rax, 32
    add edi, eax
    add [rsp+90h], rax
    RESTORE_CONTEXT
    jmp qword ptr [g_split_format_return]
split_format_plain:
    RESTORE_CONTEXT
    jmp qword ptr [g_split_plain_entry]
split_format_hook ENDP

list_format_hook PROC
    mov rcx, rdi
    mov r8, [rdi+18h]
    cmp r8, 10h
    jb list_format_inline
    mov rcx, [rdi]
list_format_inline:
    SAVE_CONTEXT
    add rcx, rsi
    call format_scalar
    cmp eax, 0ffh
    ja list_format_plain
    shr rax, 32
    add ebx, eax
    add esi, eax
    RESTORE_CONTEXT
    jmp qword ptr [g_list_format_return]
list_format_plain:
    RESTORE_CONTEXT
    jmp qword ptr [g_list_plain_entry]
list_format_hook ENDP

alternate_format_hook PROC
    SAVE_CONTEXT
    lea rcx, [rbx+rbp]
    mov rdx, r12
    sub rdx, rbx
    call format_layout_range
    cmp rax, -1
    je alternate_format_end
    cmp eax, 0ffh
    ja alternate_format_plain
    mov r10, rax
    shr r10, 32
    add rbx, r10
    add edi, r10d
    mov eax, eax
    mov [rsp+80h], rax
    RESTORE_CONTEXT
    cmp al, 0a7h
    jmp qword ptr [g_alternate_format_return]
alternate_format_end:
    RESTORE_CONTEXT
    jmp qword ptr [g_alternate_end]
alternate_format_plain:
    RESTORE_CONTEXT
    jmp qword ptr [g_alternate_plain_entry]
alternate_format_hook ENDP

alternate_advance_hook PROC
    SAVE_CONTEXT
    mov rcx, rbp
    mov rdx, r12
    mov r8, rbx
    call next_layout_offset
    mov edi, eax
    mov rbx, rax
    RESTORE_CONTEXT
    jmp qword ptr [g_alternate_advance_return]
alternate_advance_hook ENDP

split_kern_hook PROC
    cmp edx, 80h
    jae split_kern_none
    cmp r8d, 80h
    jae split_kern_none
    call qword ptr [g_map_kern_call]
    jmp split_kern_done
split_kern_none:
    xorps xmm0, xmm0
split_kern_done:
    addss xmm7, xmm0
    addss xmm8, xmm0
    jmp qword ptr [g_split_kern_return]
split_kern_hook ENDP

list_kern_hook PROC
    cmp edx, 80h
    jae list_kern_none
    cmp r8d, 80h
    jae list_kern_none
    call qword ptr [g_map_kern_call]
    jmp list_kern_done
list_kern_none:
    xorps xmm0, xmm0
list_kern_done:
    addss xmm7, xmm0
    addss xmm8, xmm0
    jmp qword ptr [g_list_kern_return]
list_kern_hook ENDP

alternate_kern_hook PROC
    cmp edx, 80h
    jae alternate_kern_none
    cmp r8d, 80h
    jae alternate_kern_none
    call qword ptr [g_map_kern_call]
    jmp alternate_kern_done
alternate_kern_none:
    xorps xmm0, xmm0
alternate_kern_done:
    addss xmm6, xmm0
    jmp qword ptr [g_alternate_kern_return]
alternate_kern_hook ENDP
END
