; ida_cloak.asm - Hex-Rays decompiler obfuscation thunks (x64 MASM)
;
; Each thunk wraps a real _impl function using a switch jump table where
; the index register is overwritten AFTER the table lookup. IDA < 9.5
; thinks the real branch is unreachable and shows only the return-0 stub.
;
; To add to project:
;   Visual Studio: Add to project -> Properties -> "MASM"
;   CMake:         enable_language(ASM_MASM)
;                  target_sources(... ida_cloak.asm)

; External C++ _impl symbols (decorated with C linkage in shadow_verify.cpp)
EXTERN ShadowSecurity_VerifySharedMemory_impl:PROC
EXTERN ShadowSecurity_VerifyLoaderPidOnce_impl:PROC
EXTERN ShadowSecurity_StartPeriodicSubscriptionCheck_impl:PROC

; ─────────────────────────────────────────────────────────────────────────────
; IDA_CLOAK_THUNK macro
;
; Generates a public thunk for <thunk_name> that forwards to <real_proc>.
; IDA decompiles the thunk as "return 0LL;" only.
;
; Parameters:
;   thunk_name  - public symbol name (what C++ code calls)
;   real_proc   - actual implementation symbol (EXTERN'd above)
; ─────────────────────────────────────────────────────────────────────────────
IDA_CLOAK_THUNK MACRO thunk_name:REQ, real_proc:REQ
    LOCAL jt, real_branch, stub_branch

thunk_name PROC PUBLIC
    ; rax = 0: select branch[0] = real code
    xor     eax, eax
    lea     r10, jt                             ; r10 = &jump_table (RIP-rel)
    movsxd  r11, DWORD PTR [r10 + rax*4]       ; r11 = jt[0] offset (real)
    mov     eax, 1                              ; IDA: "index is always 1"
    add     r11, r10                            ; r11 = absolute address
    jmp     r11                                 ; CPU -> real_branch

real_branch:
    jmp     real_proc                           ; tail-call the actual _impl

stub_branch:
    xor     eax, eax                            ; IDA decompiler: "return 0LL"
    ret

jt:
    DD  (real_branch  - jt)                     ; [0] -> real code (invisible to IDA decompiler)
    DD  (stub_branch  - jt)                     ; [1] -> return 0  (IDA sees only this)

thunk_name ENDP
ENDM

; ─────────────────────────────────────────────────────────────────────────────
.code
; ─────────────────────────────────────────────────────────────────────────────

IDA_CLOAK_THUNK ShadowSecurity_VerifySharedMemory,              ShadowSecurity_VerifySharedMemory_impl
IDA_CLOAK_THUNK ShadowSecurity_VerifyLoaderPidOnce,             ShadowSecurity_VerifyLoaderPidOnce_impl
IDA_CLOAK_THUNK ShadowSecurity_StartPeriodicSubscriptionCheck,  ShadowSecurity_StartPeriodicSubscriptionCheck_impl

END
