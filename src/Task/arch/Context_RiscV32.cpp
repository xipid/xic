/**
 * @file Context_RiscV32.cpp
 * @brief RISC-V32 context initialization.
 *
 * Companion to Context_RiscV32.S — provides xi_context_init()
 * for setting up a new RISC-V32 task context.
 */

#if defined(__riscv) && (__riscv_xlen == 32)

#include "../../../include/Task/Context.hpp"
#include "../../../include/Task/Task.hpp"

#include <cstring>

namespace Task {

/**
 * @brief Initializes a RISC-V32 TaskContext for a new task.
 *
 * Sets up:
 *   - pc: xi_context_entry_trampoline
 *   - x[0] (x1/ra): trampoline address (used by ret at end of context switch)
 *   - x[1] (x2/sp): top of stack, 16-byte aligned
 *   - x[9] (x10/a0): arg (first function argument)
 *   - All others: zero
 *
 * The context switch assembly restores all registers from the x[] array,
 * loads pc into ra, and executes `ret` which jumps to ra.
 */
void xi_context_init(TaskContext* ctx, void (*entry)(void*), void* arg,
                     u8* stack, usz stackSize) {
    // Zero the entire context.
    u8* raw = reinterpret_cast<u8*>(ctx);
    for (usz i = 0; i < sizeof(TaskContext); ++i) {
        raw[i] = 0;
    }

    // Stack grows downward. 16-byte aligned.
    u32 stackTop = reinterpret_cast<u32>(stack + stackSize);
    stackTop &= ~0xFU; // 16-byte align.

    // Set PC to the entry trampoline.
    ctx->pc = reinterpret_cast<u32>(xi_context_entry_trampoline);

    // x[0] = x1 = ra — the context switch does `ret` which jumps to ra.
    // We set ra = pc so the first switch jumps to the trampoline.
    ctx->x[0] = ctx->pc; // ra = trampoline

    // x[1] = x2 = sp
    ctx->x[1] = stackTop;

    // x[9] = x10 = a0 = first argument register.
    ctx->x[9] = reinterpret_cast<u32>(arg);

    // Store entry function pointer in x[7] = x8 = s0 (callee-saved)
    // for debugging. The trampoline reads from TaskState, not registers.
    ctx->x[7] = reinterpret_cast<u32>(entry);

    // mstatus: default 0 (machine mode with interrupts disabled initially).
    ctx->mstatus = 0;
}

// Note: xi_context_entry_trampoline is defined in Task.cpp (portable).

} // namespace Task

#endif // defined(__riscv) && (__riscv_xlen == 32)
