/* Stack switch for ibFiber on AAPCS64 (macOS arm64, and Linux arm64).
 *
 * void ib_fiber_switch(void** from_sp, void** to_sp);
 *   x0 = from_sp, x1 = to_sp.
 *
 * Saves the callee-saved integer registers (x19-x30, x29 is fp) and the
 * callee-saved SIMD registers (d8-d15). sp stays 16-byte aligned. A new
 * fiber's forged frame returns through ib_fiber_trampoline.
 *
 * Pointer authentication (arm64e) is not the ABI this builds: the forged
 * link register is the raw address of the trampoline. The macOS port is
 * the plain arm64 slice.
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

    .text
    .globl SYM(ib_fiber_switch)
#if defined(__ELF__)
    .type SYM(ib_fiber_switch), %function
#endif
    .p2align 2
SYM(ib_fiber_switch):
    .cfi_startproc
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
    mov x9, sp
    str x9, [x0]
    ldr x9, [x1]
    mov sp, x9
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
    .size SYM(ib_fiber_switch), .-SYM(ib_fiber_switch)
#endif

    .globl SYM(ib_fiber_trampoline)
#if defined(__ELF__)
    .type SYM(ib_fiber_trampoline), %function
#endif
    .p2align 2
SYM(ib_fiber_trampoline):
    .cfi_startproc
    .cfi_undefined x30
    bl PLT(ibFiberEnter)
    brk #1
    .cfi_endproc
#if defined(__ELF__)
    .size SYM(ib_fiber_trampoline), .-SYM(ib_fiber_trampoline)
#endif

#if defined(__linux__) && defined(__ELF__)
    .section .note.GNU-stack, "", %progbits
#endif
