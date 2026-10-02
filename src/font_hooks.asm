EXTERN find_loaded_glyph:PROC
EXTERN store_loaded_glyph:PROC
EXTERN allocate_unicode_glyph:PROC
EXTERN g_font_allocate:QWORD
EXTERN g_font_duplicate:QWORD
EXTERN g_font_store_return:QWORD
EXTERN g_font_initialize:QWORD
EXTERN g_font_skip:QWORD
EXTERN g_engine_new:QWORD
include hook_context.inc
.CODE
font_allocate_hook PROC
    cmp edi, 0ffh
    jbe font_allocate_native
    SAVE_CONTEXT
    mov rcx, [rbp+1130h]
    add rcx, 120h
    mov edx, edi
    call allocate_unicode_glyph
    mov [rsp+80h], rax
    RESTORE_CONTEXT
    test rax, rax
    jz font_allocate_failed
    jmp qword ptr [g_font_initialize]
font_allocate_failed:
    jmp qword ptr [g_font_skip]
font_allocate_native:
    mov ecx, 10h
    call qword ptr [g_engine_new]
    jmp qword ptr [g_font_initialize]
font_allocate_hook ENDP

font_lookup_hook PROC
    SAVE_CONTEXT
    mov rcx, [rbp+1130h]
    add rcx, 120h
    mov edx, edi
    call find_loaded_glyph
    mov [rsp+80h], rax
    RESTORE_CONTEXT
    test rax, rax
    jnz font_lookup_duplicate
    jmp qword ptr [g_font_allocate]
font_lookup_duplicate:
    jmp qword ptr [g_font_duplicate]
font_lookup_hook ENDP

font_store_hook PROC
    mov r15, [rbp+1130h]
    SAVE_CONTEXT
    lea rcx, [r15+120h]
    mov edx, edi
    mov r8, rax
    call store_loaded_glyph
    RESTORE_CONTEXT
    jmp qword ptr [g_font_store_return]
font_store_hook ENDP
END
