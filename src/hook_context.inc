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
