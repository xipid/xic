/**
 * @file Context_Xtensa.cpp
 * @brief Xtensa context initialization and entry trampoline.
 *
 * Implements xi_context_init() and xi_context_entry_trampoline() for the
 * Xtensa (ESP32-S3) architecture. These are the C/C++ companions to the
 * assembly context switch in Context_Xtensa.S.
 */

#if defined(__XTENSA__)

#include "../../include/Execution/Context.hpp"
#include "../../include/Execution/Task.hpp"

namespace Execution {

/**
 * @brief Initializes a TaskContext so that when switched to, it begins
 *        executing entry(arg) with the given stack.
 *
 * The context is set up for call0 ABI:
 *   - PC (offset 0): set to xi_context_entry_trampoline, which will call
 *     the real entry function.
 *   - a1 (SP): set to the top of the stack, 16-byte aligned (Xtensa ABI).
 *   - a2: set to arg (first argument register in call0 ABI).
 *   - PS: set with interrupts enabled (INTLEVEL=0), window exceptions
 *     enabled (WOE=1), CALLINC=0 for call0 ABI compatibility.
 *   - All other registers zero-initialized.
 *
 * The entry trampoline is used so that when the entry function returns,
 * the task is properly cleaned up by the scheduler.
 *
 * @param ctx       Context to initialize.
 * @param entry     Entry point function.
 * @param arg       Argument passed to the entry function.
 * @param stack     Base of the allocated stack memory.
 * @param stackSize Size of the stack in bytes.
 */
void xi_context_init(TaskContext* ctx, void (*entry)(void*), void* arg,
                     u8* stack, usz stackSize) {
    /* Zero the entire context structure */
    u8* raw = reinterpret_cast<u8*>(ctx);
    for (usz i = 0; i < sizeof(TaskContext); ++i) {
        raw[i] = 0;
    }

    /*
     * Stack grows downward on Xtensa. Set SP (a1) to the top of the
     * stack region, aligned to a 16-byte boundary as required by the
     * Xtensa ABI. Reserve 16 bytes at the top for the base save area
     * (the ABI requires a 16-byte save area at the top of each frame).
     */
    u32 stackTop = reinterpret_cast<u32>(stack + stackSize);
    stackTop &= ~0xFU;  /* 16-byte align */
    stackTop -= 16;     /* Reserve base save area */

    /*
     * Set the program counter to the entry trampoline. The trampoline
     * will call the actual entry function, and when it returns, will
     * mark the task as finished and yield to the scheduler.
     */
    ctx->pc = reinterpret_cast<u32>(xi_context_entry_trampoline);

    /* Stack pointer */
    ctx->a[1] = stackTop;

    /* First argument: the entry function pointer (passed as void*).
     * The trampoline will extract the actual entry and arg from TaskState,
     * but we pass arg in a2 for direct use by the trampoline. */
    ctx->a[2] = reinterpret_cast<u32>(arg);

    /*
     * Processor State register (PS):
     *   Bits [3:0]  INTLEVEL = 0  (interrupts enabled)
     *   Bits [5:4]  EXCM = 0      (not in exception mode)
     *   Bit  [6]    UM = 1        (user mode / ring 0 on ESP32)
     *   Bits [17:16] CALLINC = 0  (call0 ABI, no window increment)
     *   Bit  [18]   WOE = 1       (window overflow exceptions enabled)
     *
     * PS = (1 << 6) | (1 << 18) = 0x00040040
     *
     * Note: On ESP32-S3, WOE is typically enabled even for call0 code
     * to allow the hardware to handle window overflows if windowed
     * code is called. If running pure call0, WOE can be 0.
     * We enable it for compatibility.
     */
    ctx->ps = (1U << 6) | (1U << 18);

    /* SAR, loop regs, window regs all zeroed by the memset above */

    /* WINDOWBASE = 0, WINDOWSTART = 1 (only window 0 is active) */
    ctx->windowBase = 0;
    ctx->windowStart = 1;

    /*
     * Store the entry function pointer and arg in the spill area
     * for the trampoline to retrieve. This avoids needing extra
     * registers or global state.
     *
     * windowSpill[0..3] = entry function pointer
     * windowSpill[4..7] = arg pointer
     */
    u32* spill32 = reinterpret_cast<u32*>(ctx->windowSpill);
    spill32[0] = reinterpret_cast<u32>(entry);
    spill32[1] = reinterpret_cast<u32>(arg);
}

/**
 * @brief Entry trampoline called when a newly initialized task context
 *        is switched to for the first time.
 *
 * The trampoline:
 *   1. Retrieves the entry function and argument from the current task's
 *      TaskState (set up by Task::setEntry / Task::start).
 *   2. Calls the entry function with the argument.
 *   3. When the entry function returns, marks the task as Finished.
 *   4. Yields to the scheduler so another task can run.
 *
 * This function never returns normally — after yield, the scheduler
 * will never reschedule a Finished task.
 *
 * @param arg  The argument pointer (set in a2 by xi_context_init).
 *             On ESP32, this is typically the entryArg from TaskState.
 */
extern "C" void xi_context_entry_trampoline(void* arg) {
    if (!Task::instance) {
        /* No scheduler running — spin forever.
         * This should never happen in normal operation. */
        for (;;) {
            __asm__ volatile("waiti 0");
        }
    }

    /* Get the current core ID and the running task */
    usz coreId = xi_current_core();
    TaskState* task = Task::currentTask(coreId);

    if (task && task->entryFn) {
        /* Call the actual entry function */
        task->entryFn(task->entryArg);
    }

    /* Entry function returned — mark the task as finished */
    if (task) {
        task->status = TaskStatus::Finished;
    }

    /* Yield to the scheduler. This will context-switch away from this
     * task and it will never be scheduled again. */
    Task::current().yield(coreId);

    /* Should never reach here, but just in case: */
    for (;;) {
        __asm__ volatile("waiti 0");
    }
}

} // namespace Execution

#endif /* defined(__XTENSA__) */
