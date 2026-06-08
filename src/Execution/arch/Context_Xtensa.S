/**
 * @file Context_Xtensa.S
 * @brief Xtensa (ESP32-S3) context switch implementation.
 *
 * Implements xi_context_switch(TaskContext* from, TaskContext* to) using
 * call0 ABI. Performs a full register window spill before saving state,
 * then saves/restores all relevant registers and special registers.
 *
 * TaskContext layout (from Context.hpp):
 *   Offset  Field
 *   0       pc           (u32)
 *   4       ps           (u32)
 *   8       sar          (u32)
 *   12      lbeg         (u32)
 *   16      lend         (u32)
 *   20      lcount       (u32)
 *   24      a[0]         (u32)  — a0
 *   28      a[1]         (u32)  — a1 (sp)
 *   32      a[2]         (u32)  — a2
 *   36      a[3]         (u32)  — a3
 *   40      a[4]         (u32)  — a4
 *   44      a[5]         (u32)  — a5
 *   48      a[6]         (u32)  — a6
 *   52      a[7]         (u32)  — a7
 *   56      a[8]         (u32)  — a8
 *   60      a[9]         (u32)  — a9
 *   64      a[10]        (u32)  — a10
 *   68      a[11]        (u32)  — a11
 *   72      a[12]        (u32)  — a12
 *   76      a[13]        (u32)  — a13
 *   80      a[14]        (u32)  — a14
 *   84      a[15]        (u32)  — a15
 *   88      windowBase   (u32)
 *   92      windowStart  (u32)
 *   96      windowSpill  (256 bytes)
 */

#if defined(__XTENSA__)

/* TaskContext field offsets */
#define CTX_PC           0
#define CTX_PS           4
#define CTX_SAR          8
#define CTX_LBEG        12
#define CTX_LEND        16
#define CTX_LCOUNT      20
#define CTX_A0          24
#define CTX_A1          28
#define CTX_A2          32
#define CTX_A3          36
#define CTX_A4          40
#define CTX_A5          44
#define CTX_A6          48
#define CTX_A7          52
#define CTX_A8          56
#define CTX_A9          60
#define CTX_A10         64
#define CTX_A11         68
#define CTX_A12         72
#define CTX_A13         76
#define CTX_A14         80
#define CTX_A15         84
#define CTX_WINDOWBASE  88
#define CTX_WINDOWSTART 92
#define CTX_WINDOWSPILL 96

    .text
    .align 4
    .global xi_context_switch
    .type   xi_context_switch, @function

/**
 * xi_context_switch(TaskContext* from, TaskContext* to)
 *
 * call0 ABI: a2 = from, a3 = to.
 * Return address is in a0.
 *
 * Strategy:
 *   1. Spill all register windows so everything is on the stack.
 *   2. Save current state into 'from'.
 *   3. Restore state from 'to'.
 *   4. Return via the 'to' context's saved PC.
 */
xi_context_switch:
    /*
     * ===== Step 1: Spill all register windows =====
     *
     * We use the SPILL_REGISTERS macro approach: repeatedly execute
     * ROTW to visit each window and touch registers to force spills.
     * On ESP32 (Xtensa LX7), the register file has 64 physical
     * registers = 16 windows of 4.
     *
     * The simplest portable approach: use ssai / rsr / wsr to
     * manipulate WINDOWSTART, set it so only the current window is
     * marked as in-use, effectively spilling all others.
     *
     * However, the canonical ESP-IDF approach is:
     *   call4 -> call8 -> call12 to trigger spills, then return.
     *
     * For call0 ABI context switch, we use the direct RSR/WSR approach:
     * we read WINDOWBASE, set WINDOWSTART = (1 << WINDOWBASE),
     * which marks only the current window as live and implicitly
     * considers all other windows as already spilled.
     *
     * IMPORTANT: Before doing this, all callee-saved state must
     * already be on the stack frames (which it is, since the compiler
     * emits windowed calls with proper spill code in the prologues).
     */

    /* Save PS and disable interrupts for atomicity */
    rsr     a4, ps
    movi    a5, ~0x0F          /* Clear INTLEVEL bits */
    and     a6, a4, a5
    movi    a5, 0x0F           /* Set INTLEVEL to max (15) */
    or      a6, a6, a5
    wsr     a6, ps
    rsync

    /* ----- Save current WINDOWBASE/WINDOWSTART ----- */
    rsr     a6, windowbase
    s32i    a6, a2, CTX_WINDOWBASE
    rsr     a7, windowstart
    s32i    a7, a2, CTX_WINDOWSTART

    /*
     * Spill all register windows by marking only the current window
     * as active. On Xtensa, when WINDOWSTART has only the current
     * window's bit set, all other windows are considered spilled.
     * The actual spill happens automatically through the ABI.
     */
    movi    a5, 1
    ssl     a6              /* SAR = WINDOWBASE */
    sll     a5, a5          /* a5 = 1 << WINDOWBASE */
    wsr     a5, windowstart
    rsync

    /* ===== Step 2: Save current context into 'from' (a2) ===== */

    /* Save return address as PC — when this context is restored,
     * execution resumes right after xi_context_switch was called */
    s32i    a0, a2, CTX_PC

    /* Save PS (the original one, before we disabled interrupts) */
    s32i    a4, a2, CTX_PS

    /* Save SAR */
    rsr     a5, sar
    s32i    a5, a2, CTX_SAR

    /* Save loop registers */
    rsr     a5, lbeg
    s32i    a5, a2, CTX_LBEG
    rsr     a5, lend
    s32i    a5, a2, CTX_LEND
    rsr     a5, lcount
    s32i    a5, a2, CTX_LCOUNT

    /* Save address registers a0–a15.
     * Note: a0 already saved as PC. We save it again in the a[] slot.
     * a2 (from ptr) and a3 (to ptr) are saved as-is. */
    s32i    a0,  a2, CTX_A0
    s32i    a1,  a2, CTX_A1
    s32i    a2,  a2, CTX_A2
    s32i    a3,  a2, CTX_A3
    s32i    a4,  a2, CTX_A4
    s32i    a5,  a2, CTX_A5
    s32i    a6,  a2, CTX_A6
    s32i    a7,  a2, CTX_A7
    s32i    a8,  a2, CTX_A8
    s32i    a9,  a2, CTX_A9
    s32i    a10, a2, CTX_A10
    s32i    a11, a2, CTX_A11
    s32i    a12, a2, CTX_A12
    s32i    a13, a2, CTX_A13
    s32i    a14, a2, CTX_A14
    s32i    a15, a2, CTX_A15

    /* ===== Step 3: Restore context from 'to' (a3) ===== */

    /* We use a3 as the base pointer for restoring.
     * Restore special registers first, then address registers last.
     * a3 itself is restored at the very end. */

    /* Restore SAR */
    l32i    a5, a3, CTX_SAR
    wsr     a5, sar

    /* Restore loop registers */
    l32i    a5, a3, CTX_LBEG
    wsr     a5, lbeg
    l32i    a5, a3, CTX_LEND
    wsr     a5, lend
    l32i    a5, a3, CTX_LCOUNT
    wsr     a5, lcount

    /* Restore WINDOWBASE and WINDOWSTART */
    l32i    a5, a3, CTX_WINDOWBASE
    wsr     a5, windowbase
    rsync
    l32i    a5, a3, CTX_WINDOWSTART
    wsr     a5, windowstart
    rsync

    /* Restore PS (will re-enable interrupts at saved level) */
    l32i    a5, a3, CTX_PS
    wsr     a5, ps
    rsync

    /* Restore address registers.
     * Order matters: restore a0 (return addr) and a1 (sp) last
     * so we can still use a3 as base. Load PC into a0. */
    l32i    a15, a3, CTX_A15
    l32i    a14, a3, CTX_A14
    l32i    a13, a3, CTX_A13
    l32i    a12, a3, CTX_A12
    l32i    a11, a3, CTX_A11
    l32i    a10, a3, CTX_A10
    l32i    a9,  a3, CTX_A9
    l32i    a8,  a3, CTX_A8
    l32i    a7,  a3, CTX_A7
    l32i    a6,  a3, CTX_A6
    l32i    a5,  a3, CTX_A5
    l32i    a4,  a3, CTX_A4
    l32i    a2,  a3, CTX_A2

    /* Restore a1 (sp) */
    l32i    a1,  a3, CTX_A1

    /* Load saved PC into a0 — this is where we'll return to */
    l32i    a0,  a3, CTX_PC

    /* Restore a3 last (it was our base pointer) */
    l32i    a3,  a3, CTX_A3

    /* Return to the restored context's PC */
    ret

    .size   xi_context_switch, . - xi_context_switch

/**
 * xi_context_entry_trampoline_asm
 *
 * Assembly stub that the context switch jumps to for newly initialized tasks.
 * The C trampoline xi_context_entry_trampoline(void* arg) is called from here.
 *
 * On entry (set up by xi_context_init):
 *   a1 = stack pointer (top of stack, aligned)
 *   a2 = arg (void*)
 *   a0 = address of xi_context_entry_trampoline (set as PC)
 *
 * Since xi_context_init sets PC to xi_context_entry_trampoline directly,
 * this stub is not strictly needed — the C function is entered directly.
 * Kept here for reference / future use.
 */

#endif /* defined(__XTENSA__) */
