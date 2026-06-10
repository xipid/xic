/**
 * @file Context_Amd64.S
 * @brief x86_64 (AMD64) context switch implementation for the Task subsystem.
 *
 * Provides xi_context_switch which saves the current CPU state into a
 * TaskContext struct and restores state from another TaskContext.
 *
 * TaskContext layout (from Context.hpp):
 *   Offset  0: rip      (u64)
 *   Offset  8: rsp      (u64)
 *   Offset 16: rbp      (u64)
 *   Offset 24: rbx      (u64)
 *   Offset 32: r12      (u64)
 *   Offset 40: r13      (u64)
 *   Offset 48: r14      (u64)
 *   Offset 56: r15      (u64)
 *   Offset 64: rflags   (u64)
 *   Offset 72: rdi      (u64)
 *   Offset 80: rsi      (u64)
 *   Offset 88: rax      (u64)
 *   Offset 96: rcx      (u64)
 *   Offset104: rdx      (u64)
 *   Offset112: r8       (u64)
 *   Offset120: r9       (u64)
 *   Offset128: r10      (u64)
 *   Offset136: r11      (u64)
 *   Offset144: fxsave   (512 bytes, 16-byte aligned)
 */

#if defined(__x86_64__) || defined(_M_X64)

/* Struct field offsets */
.set CTX_RIP,       0
.set CTX_RSP,       8
.set CTX_RBP,      16
.set CTX_RBX,      24
.set CTX_R12,      32
.set CTX_R13,      40
.set CTX_R14,      48
.set CTX_R15,      56
.set CTX_RFLAGS,   64
.set CTX_RDI,      72
.set CTX_RSI,      80
.set CTX_RAX,      88
.set CTX_RCX,      96
.set CTX_RDX,     104
.set CTX_R8,      112
.set CTX_R9,      120
.set CTX_R10,     128
.set CTX_R11,     136

.text
.globl xi_context_switch
.type xi_context_switch, @function
.align 16

/**
 * void xi_context_switch(TaskContext* from, TaskContext* to)
 *
 * System V AMD64 ABI:
 *   rdi = from (TaskContext* to save into)
 *   rsi = to   (TaskContext* to restore from)
 *
 * We save callee-saved registers (rbx, rbp, r12-r15), rsp, rflags,
 * and the return address as rip. On restore, we push the target rip
 * onto the target stack and ret into it.
 */
xi_context_switch:
    /* ---- Save current context into *from (rdi) ---- */

    /* Save the return address (rip of caller) from the stack. */
    movq    (%rsp), %rax
    movq    %rax, CTX_RIP(%rdi)

    /* Save rsp (as it was before the CALL that brought us here,
       i.e. rsp + 8 to skip the return address). */
    leaq    8(%rsp), %rax
    movq    %rax, CTX_RSP(%rdi)

    /* Save callee-saved registers. */
    movq    %rbp, CTX_RBP(%rdi)
    movq    %rbx, CTX_RBX(%rdi)
    movq    %r12, CTX_R12(%rdi)
    movq    %r13, CTX_R13(%rdi)
    movq    %r14, CTX_R14(%rdi)
    movq    %r15, CTX_R15(%rdi)

    /* Save rflags. */
    pushfq
    popq    %rax
    movq    %rax, CTX_RFLAGS(%rdi)

    /* ---- Restore context from *to (rsi) ---- */

    /* Restore rflags. */
    movq    CTX_RFLAGS(%rsi), %rax
    pushq   %rax
    popfq

    /* Restore callee-saved registers. */
    movq    CTX_RBP(%rsi), %rbp
    movq    CTX_RBX(%rsi), %rbx
    movq    CTX_R12(%rsi), %r12
    movq    CTX_R13(%rsi), %r13
    movq    CTX_R14(%rsi), %r14
    movq    CTX_R15(%rsi), %r15

    /* Restore rsp. */
    movq    CTX_RSP(%rsi), %rsp

    /* Restore rdi (the 'to' context may have set rdi for trampoline arg). */
    movq    CTX_RDI(%rsi), %rdi

    /* Push the target rip onto the new stack and ret into it. */
    pushq   CTX_RIP(%rsi)
    ret

.size xi_context_switch, . - xi_context_switch

#endif /* defined(__x86_64__) || defined(_M_X64) */
