/* Stack switch for ibFiber on SysV x86-64 (Linux, and macOS x64 if it is ever built).
 *
 * void ibFiberSwitch(void** from_sp, void** to_sp);
 *   rdi / from_sp receives the saved stack pointer.
 *   rsi / to_sp  supplies the stack to resume.
 *
 * The image below the saved sp matches InitPosixStack in fiber.cpp:
 *   mxcsr+fcw, r15, r14, r13, r12, rbx, rbp, and the return address the
 *   call instruction pushed. A brand-new fiber forges that image so the
 *   ret below lands in ibFiberTrampoline with the stack aligned for a call.
 *
 * Callee-saved registers only. XMM is caller-saved on SysV; MXCSR and the
 * x87 control word are not, so they travel with the fiber. No red zone is
 * live across the call that entered here.
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
    .type SYM(ibFiberSwitch), @function
#endif
    .p2align 4
SYM(ibFiberSwitch):
    .cfi_startproc
    pushq %rbp
    .cfi_def_cfa_offset 16
    .cfi_offset rbp, -16
    pushq %rbx
    .cfi_def_cfa_offset 24
    .cfi_offset rbx, -24
    pushq %r12
    .cfi_def_cfa_offset 32
    .cfi_offset r12, -32
    pushq %r13
    .cfi_def_cfa_offset 40
    .cfi_offset r13, -40
    pushq %r14
    .cfi_def_cfa_offset 48
    .cfi_offset r14, -48
    pushq %r15
    .cfi_def_cfa_offset 56
    .cfi_offset r15, -56
    subq $8, %rsp
    .cfi_def_cfa_offset 64
    stmxcsr (%rsp)
    fnstcw  4(%rsp)
    movq %rsp, (%rdi)
    movq (%rsi), %rsp
    ldmxcsr (%rsp)
    fldcw   4(%rsp)
    addq $8, %rsp
    popq %r15
    popq %r14
    popq %r13
    popq %r12
    popq %rbx
    popq %rbp
    .cfi_def_cfa_offset 8
    ret
    .cfi_endproc
#if defined(__ELF__)
    .size SYM(ibFiberSwitch), .-SYM(ibFiberSwitch)
#endif

/* Entered by the ret above on a fiber's first switch. rsp is 16-byte
 * aligned here (the same alignment a call requires), which is what
 * ibFiberEnter needs. */
    .globl SYM(ibFiberTrampoline)
    HIDDEN(ibFiberTrampoline)
#if defined(__ELF__)
    .type SYM(ibFiberTrampoline), @function
#endif
    .p2align 4
SYM(ibFiberTrampoline):
    .cfi_startproc
    .cfi_undefined rip
    call PLT(ibFiberEnter)
    ud2
    .cfi_endproc
#if defined(__ELF__)
    .size SYM(ibFiberTrampoline), .-SYM(ibFiberTrampoline)
#endif

#if defined(__linux__) && defined(__ELF__)
    .section .note.GNU-stack, "", @progbits
#endif
