/**
 * @file Context_RiscV32.S
 * @brief RISC-V32 context switch implementation for the Tasker subsystem.
 *
 * Implements xi_context_switch(TaskContext* from, TaskContext* to) which
 * saves the full register state into 'from' and restores from 'to'.
 *
 * TaskContext layout (from Context.hpp):
 *   Offset 0:   pc       (u32) — program counter (saved as ra)
 *   Offset 4:   mstatus  (u32) — machine status register
 *   Offset 8:   x[0]     (u32) — x1  / ra
 *   Offset 12:  x[1]     (u32) — x2  / sp
 *   Offset 16:  x[2]     (u32) — x3  / gp
 *   Offset 20:  x[3]     (u32) — x4  / tp
 *   Offset 24:  x[4]     (u32) — x5  / t0
 *   Offset 28:  x[5]     (u32) — x6  / t1
 *   Offset 32:  x[6]     (u32) — x7  / t2
 *   Offset 36:  x[7]     (u32) — x8  / s0 (fp)
 *   Offset 40:  x[8]     (u32) — x9  / s1
 *   Offset 44:  x[9]     (u32) — x10 / a0
 *   Offset 48:  x[10]    (u32) — x11 / a1
 *   Offset 52:  x[11]    (u32) — x12 / a2
 *   Offset 56:  x[12]    (u32) — x13 / a3
 *   Offset 60:  x[13]    (u32) — x14 / a4
 *   Offset 64:  x[14]    (u32) — x15 / a5
 *   Offset 68:  x[15]    (u32) — x16 / a6
 *   Offset 72:  x[16]    (u32) — x17 / a7
 *   Offset 76:  x[17]    (u32) — x18 / s2
 *   Offset 80:  x[18]    (u32) — x19 / s3
 *   Offset 84:  x[19]    (u32) — x20 / s4
 *   Offset 88:  x[20]    (u32) — x21 / s5
 *   Offset 92:  x[21]    (u32) — x22 / s6
 *   Offset 96:  x[22]    (u32) — x23 / s7
 *   Offset 100: x[23]    (u32) — x24 / s8
 *   Offset 104: x[24]    (u32) — x25 / s9
 *   Offset 108: x[25]    (u32) — x26 / s10
 *   Offset 112: x[26]    (u32) — x27 / s11
 *   Offset 116: x[27]    (u32) — x28 / t3
 *   Offset 120: x[28]    (u32) — x29 / t4
 *   Offset 124: x[29]    (u32) — x30 / t5
 *   Offset 128: x[30]    (u32) — x31 / t6
 */

#if defined(__riscv) && (__riscv_xlen == 32)

/* --------------------------------------------------------------------------
 * Offset constants for TaskContext
 * -------------------------------------------------------------------------- */
#define CTX_PC       0
#define CTX_MSTATUS  4
#define CTX_X1       8    /* ra  */
#define CTX_X2       12   /* sp  */
#define CTX_X3       16   /* gp  */
#define CTX_X4       20   /* tp  */
#define CTX_X5       24   /* t0  */
#define CTX_X6       28   /* t1  */
#define CTX_X7       32   /* t2  */
#define CTX_X8       36   /* s0 / fp */
#define CTX_X9       40   /* s1  */
#define CTX_X10      44   /* a0  */
#define CTX_X11      48   /* a1  */
#define CTX_X12      52   /* a2  */
#define CTX_X13      56   /* a3  */
#define CTX_X14      60   /* a4  */
#define CTX_X15      64   /* a5  */
#define CTX_X16      68   /* a6  */
#define CTX_X17      72   /* a7  */
#define CTX_X18      76   /* s2  */
#define CTX_X19      80   /* s3  */
#define CTX_X20      84   /* s4  */
#define CTX_X21      88   /* s5  */
#define CTX_X22      92   /* s6  */
#define CTX_X23      96   /* s7  */
#define CTX_X24      100  /* s8  */
#define CTX_X25      104  /* s9  */
#define CTX_X26      108  /* s10 */
#define CTX_X27      112  /* s11 */
#define CTX_X28      116  /* t3  */
#define CTX_X29      120  /* t4  */
#define CTX_X30      124  /* t5  */
#define CTX_X31      128  /* t6  */

    .section .text
    .globl xi_context_switch
    .type  xi_context_switch, @function
    .align 2

/**
 * void xi_context_switch(TaskContext* from, TaskContext* to);
 *
 * Arguments:
 *   a0 = pointer to 'from' TaskContext (save current state here)
 *   a1 = pointer to 'to'   TaskContext (restore state from here)
 *
 * On entry, ra holds the return address of the caller — this becomes
 * the saved pc for the 'from' context. When 'from' is restored later,
 * execution resumes as if xi_context_switch had just returned.
 */
xi_context_switch:
    /* ---- Save current state into 'from' (a0) ---- */

    /* Save ra as the program counter (return address) */
    sw   ra,  CTX_PC(a0)

    /* Save all general-purpose registers x1..x31 */
    sw   x1,  CTX_X1(a0)       /* ra  */
    sw   x2,  CTX_X2(a0)       /* sp  */
    sw   x3,  CTX_X3(a0)       /* gp  */
    sw   x4,  CTX_X4(a0)       /* tp  */
    sw   x5,  CTX_X5(a0)       /* t0  */
    sw   x6,  CTX_X6(a0)       /* t1  */
    sw   x7,  CTX_X7(a0)       /* t2  */
    sw   x8,  CTX_X8(a0)       /* s0  */
    sw   x9,  CTX_X9(a0)       /* s1  */
    sw   x10, CTX_X10(a0)      /* a0 (pointer to from — saved for completeness) */
    sw   x11, CTX_X11(a0)      /* a1 (pointer to to) */
    sw   x12, CTX_X12(a0)      /* a2  */
    sw   x13, CTX_X13(a0)      /* a3  */
    sw   x14, CTX_X14(a0)      /* a4  */
    sw   x15, CTX_X15(a0)      /* a5  */
    sw   x16, CTX_X16(a0)      /* a6  */
    sw   x17, CTX_X17(a0)      /* a7  */
    sw   x18, CTX_X18(a0)      /* s2  */
    sw   x19, CTX_X19(a0)      /* s3  */
    sw   x20, CTX_X20(a0)      /* s4  */
    sw   x21, CTX_X21(a0)      /* s5  */
    sw   x22, CTX_X22(a0)      /* s6  */
    sw   x23, CTX_X23(a0)      /* s7  */
    sw   x24, CTX_X24(a0)      /* s8  */
    sw   x25, CTX_X25(a0)      /* s9  */
    sw   x26, CTX_X26(a0)      /* s10 */
    sw   x27, CTX_X27(a0)      /* s11 */
    sw   x28, CTX_X28(a0)      /* t3  */
    sw   x29, CTX_X29(a0)      /* t4  */
    sw   x30, CTX_X30(a0)      /* t5  */
    sw   x31, CTX_X31(a0)      /* t6  */

    /* Save mstatus (only accessible in M-mode; on user-mode ESP-IDF
       tasks this is a NOP-equivalent — we read it if available). */
#if defined(__riscv_privileged)
    csrr  t0, mstatus
    sw    t0, CTX_MSTATUS(a0)
#else
    sw    zero, CTX_MSTATUS(a0)
#endif

    /* ---- Restore state from 'to' (a1) ---- */

    /* Restore mstatus first (before clobbering t0) */
#if defined(__riscv_privileged)
    lw    t0, CTX_MSTATUS(a1)
    csrw  mstatus, t0
#endif

    /* Load the target pc into ra — we'll jump to it at the end */
    lw   ra,  CTX_PC(a1)

    /* Restore all general-purpose registers except a1 (x11) last,
       because we still need the 'to' pointer. Also skip ra (x1)
       since we already loaded it from pc. */

    /*  Restore registers that don't conflict with a1 */
    lw   x2,  CTX_X2(a1)       /* sp  */
    lw   x3,  CTX_X3(a1)       /* gp  */
    lw   x4,  CTX_X4(a1)       /* tp  */
    lw   x5,  CTX_X5(a1)       /* t0  */
    lw   x6,  CTX_X6(a1)       /* t1  */
    lw   x7,  CTX_X7(a1)       /* t2  */
    lw   x8,  CTX_X8(a1)       /* s0  */
    lw   x9,  CTX_X9(a1)       /* s1  */
    lw   x10, CTX_X10(a1)      /* a0  */
    /* Skip x11 (a1) — restore last */
    lw   x12, CTX_X12(a1)      /* a2  */
    lw   x13, CTX_X13(a1)      /* a3  */
    lw   x14, CTX_X14(a1)      /* a4  */
    lw   x15, CTX_X15(a1)      /* a5  */
    lw   x16, CTX_X16(a1)      /* a6  */
    lw   x17, CTX_X17(a1)      /* a7  */
    lw   x18, CTX_X18(a1)      /* s2  */
    lw   x19, CTX_X19(a1)      /* s3  */
    lw   x20, CTX_X20(a1)      /* s4  */
    lw   x21, CTX_X21(a1)      /* s5  */
    lw   x22, CTX_X22(a1)      /* s6  */
    lw   x23, CTX_X23(a1)      /* s7  */
    lw   x24, CTX_X24(a1)      /* s8  */
    lw   x25, CTX_X25(a1)      /* s9  */
    lw   x26, CTX_X26(a1)      /* s10 */
    lw   x27, CTX_X27(a1)      /* s11 */
    lw   x28, CTX_X28(a1)      /* t3  */
    lw   x29, CTX_X29(a1)      /* t4  */
    lw   x30, CTX_X30(a1)      /* t5  */
    lw   x31, CTX_X31(a1)      /* t6  */

    /* Restore x1 (ra) from the x[0] slot — this must match pc
       for a newly initialized context. For a saved context, x[0]
       holds the ra at the point of save. We use the pc slot value
       (already in ra) as the actual jump target. */
    lw   x1,  CTX_X1(a1)       /* ra from x[] array */

    /* Finally restore a1 (x11) — last because we used it as base */
    lw   x11, CTX_X11(a1)      /* a1  */

    /* Jump to the restored program counter.
       For a returning context, this is the ra after xi_context_switch call.
       For a newly-initialized context, this is the trampoline. */
    ret

    .size xi_context_switch, . - xi_context_switch

#endif /* defined(__riscv) && (__riscv_xlen == 32) */
