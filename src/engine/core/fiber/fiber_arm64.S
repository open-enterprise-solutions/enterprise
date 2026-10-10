/* Stack switch for ibFiber on AAPCS64 (macOS arm64, and Linux arm64).
 *
 * void ibFiberSwitch(void** from_sp, void** to_sp);
 *   x0 = from_sp, x1 = to_sp.
 *
 * Saves the callee-saved integer registers (x19-x30, x29 is fp), the
 * callee-saved SIMD registers (d8-d15), and FPCR. FPCR is not callee-saved
 * by AAPCS, and it is not switched with the thread: a fiber that changes
 * rounding must not hand that to the next session on the same thread.
 * sp stays 16-byte aligned. A new fiber's forged frame returns through
 * ibFiberTrampoline. The forged FPCR slot is 0, the reset value.
 *
 * Pointer authentication (arm64e) is not the ABI this builds: the forged
 * link register is the raw address of the trampoline. The note at the bottom
 * does not claim PAC. BTI landing pads are emitted only when the compiler
 * already asked for them, so this file does not clear that bit. The macOS
 * port is the plain arm64 slice.
 *
 * TSan does not see this ret. The caller notifies it with
 * __tsan_switch_to_fiber immediately before the call.
 */

#if defined(__APPLE__)
#  define SYM(name) _##name
#else
#  define SYM(name) name
#endif

#if defined(__ELF__)
#  define PLT(name) name@PLT
#else
#  define PLT(name) SYM(name)
#endif

/* Global for the linker, not exported: core shows what CORE_API names and nothing else (CMakeLists.txt), and the
 * switch is called from fiber.cpp alone. */
#if defined(__APPLE__)
#  define HIDDEN(name) .private_extern SYM(name)
#else
#  define HIDDEN(name) .hidden SYM(name)
#endif

    .text
    .globl SYM(ibFiberSwitch)
    HIDDEN(ibFiberSwitch)
#if defined(__ELF__)
    .type SYM(ibFiberSwitch), %function
#endif
    .p2align 2
SYM(ibFiberSwitch):
    .cfi_startproc
#if defined(__ARM_FEATURE_BTI_DEFAULT)
    bti c
#endif
    stp x29, x30, [sp, #-16]!
    .cfi_def_cfa_offset 16
    .cfi_offset x29, -16
    .cfi_offset x30, -8
    stp x27, x28, [sp, #-16]!
    stp x25, x26, [sp, #-16]!
    stp x23, x24, [sp, #-16]!
    stp x21, x22, [sp, #-16]!
    stp x19, x20, [sp, #-16]!
    stp d14, d15, [sp, #-16]!
    stp d12, d13, [sp, #-16]!
    stp d10, d11, [sp, #-16]!
    stp d8,  d9,  [sp, #-16]!
    mrs x9, fpcr
    str x9, [sp, #-16]!
    mov x9, sp
    str x9, [x0]
    ldr x9, [x1]
    mov sp, x9
    ldr x9, [sp], #16
    msr fpcr, x9
    ldp d8,  d9,  [sp], #16
    ldp d10, d11, [sp], #16
    ldp d12, d13, [sp], #16
    ldp d14, d15, [sp], #16
    ldp x19, x20, [sp], #16
    ldp x21, x22, [sp], #16
    ldp x23, x24, [sp], #16
    ldp x25, x26, [sp], #16
    ldp x27, x28, [sp], #16
    ldp x29, x30, [sp], #16
    .cfi_def_cfa_offset 0
    ret
    .cfi_endproc
#if defined(__ELF__)
    .size SYM(ibFiberSwitch), .-SYM(ibFiberSwitch)
#endif

    .globl SYM(ibFiberTrampoline)
    HIDDEN(ibFiberTrampoline)
#if defined(__ELF__)
    .type SYM(ibFiberTrampoline), %function
#endif
    .p2align 2
SYM(ibFiberTrampoline):
    .cfi_startproc
    .cfi_undefined x30
#if defined(__ARM_FEATURE_BTI_DEFAULT)
    bti c
#endif
    bl PLT(ibFiberEnter)
    brk #1
    .cfi_endproc
#if defined(__ELF__)
    .size SYM(ibFiberTrampoline), .-SYM(ibFiberTrampoline)
#endif

#if defined(__linux__) && defined(__ELF__)
    .section .note.GNU-stack, "", %progbits
#endif

/* Same rule as fiber_x64.S: a missing GNU property note clears the bit for
 * the whole DSO. Emitted only when the compiler is already asking for BTI,
 * and the bit is BTI alone. PAC is not claimed — the forged x30 is a raw
 * address, and signing it is a separate decision. */
#if defined(__linux__) && defined(__ELF__) && defined(__ARM_FEATURE_BTI_DEFAULT)
    .section .note.gnu.property, "a"
    .align 3
    .long 1f - 0f
    .long 4f - 1f
    .long 5
0:
    .string "GNU"
1:
    .align 3
    .long 0xc0000000
    .long 3f - 2f
2:
    .long 1
3:
    .align 3
4:
#endif
