option casemap:none
EXTERN version_exports:QWORD

FORWARD MACRO symbol, slot
PUBLIC symbol
symbol PROC
    jmp QWORD PTR [version_exports + slot * 8]
symbol ENDP
ENDM

.code
FORWARD proxy_GetFileVersionInfoA, 0
FORWARD proxy_GetFileVersionInfoByHandle, 1
FORWARD proxy_GetFileVersionInfoExA, 2
FORWARD proxy_GetFileVersionInfoExW, 3
FORWARD proxy_GetFileVersionInfoSizeA, 4
FORWARD proxy_GetFileVersionInfoSizeExA, 5
FORWARD proxy_GetFileVersionInfoSizeExW, 6
FORWARD proxy_GetFileVersionInfoSizeW, 7
FORWARD proxy_GetFileVersionInfoW, 8
FORWARD proxy_VerFindFileA, 9
FORWARD proxy_VerFindFileW, 10
FORWARD proxy_VerInstallFileA, 11
FORWARD proxy_VerInstallFileW, 12
FORWARD proxy_VerLanguageNameA, 13
FORWARD proxy_VerLanguageNameW, 14
FORWARD proxy_VerQueryValueA, 15
FORWARD proxy_VerQueryValueW, 16
END
