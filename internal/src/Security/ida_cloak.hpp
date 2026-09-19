#pragma once
/*
 * ida_cloak.hpp - Hex-Rays decompiler obfuscation via switch jump table trick.
 *
 * Technique: IDA incorrectly optimises a switch jump table when the index
 * register is overwritten AFTER the table lookup but BEFORE the jmp.
 *
 *   lea  r10, jump_table
 *   movsxd r11, dword [r10 + rax*4]  ; rax=0 -> loads branch[0] address
 *   mov  eax, 1                       ; IDA thinks index is ALWAYS 1
 *   add  r11, r10
 *   jmp  r11                          ; CPU jumps to branch[0] (real code)
 *                                     ; IDA decompiles only branch[1] (stub)
 *
 * IDA Pro < 9.5 removes branch[0] as "unreachable" and only shows the stub.
 *
 * Usage (in .asm files): use the IDA_CLOAK_THUNK macro.
 * Usage (in C++): declare extern functions from the .asm file and call those
 *                 instead of the real _impl versions.
 *
 * Applied to:
 *   - shadow_verify.cpp  : VerifySharedMemory, VerifyLoaderPidOnce,
 *                          StartPeriodicSubscriptionCheck
 *   - launcher main.cpp  : Phase1_Prepare, Phase2_Inject
 */
