EXTERN decode_z:PROC
EXTERN next_layout_scalar:PROC
EXTERN g_bitmap_advance_return:QWORD
EXTERN g_list_measure_return:QWORD
EXTERN g_list_advance_return:QWORD
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
END
