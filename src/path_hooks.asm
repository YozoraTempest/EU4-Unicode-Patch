EXTERN g_path_pair_return:QWORD
EXTERN g_wide_compare_left_return:QWORD
EXTERN g_wide_compare_right_return:QWORD
.CODE
path_pair_hook PROC
    ; The native UTF-16 decoder validates both surrogates before this branch.
    ; Restore the missing supplementary-plane offset, then use its bounded
    ; UTF-8 encoder and existing filename validation.
    mov ecx, edx
    add r11, 2
    shl ecx, 10
    or ecx, eax
    add ecx, 10000h
    jmp qword ptr [g_path_pair_return]
path_pair_hook ENDP

wide_compare_left_hook PROC
    shl ecx, 10
    or ecx, eax
    add ecx, 10000h
    jmp qword ptr [g_wide_compare_left_return]
wide_compare_left_hook ENDP

wide_compare_right_hook PROC
    shl ecx, 10
    or ecx, eax
    add ecx, 10000h
    jmp qword ptr [g_wide_compare_right_return]
wide_compare_right_hook ENDP
END
