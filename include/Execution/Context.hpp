/**
 * @file Context.hpp
 * @brief Portable context switch interface for the Tasker subsystem.
 *
 * Each architecture provides its own TaskContext layout and
 * xi_context_switch / xi_context_init implementations in assembly.
 */

#ifndef XI_EXECUTION_CONTEXT_HPP
#define XI_EXECUTION_CONTEXT_HPP

#include "../Xi/Primitives.hpp"

namespace Execution {

using namespace Xi;

// -------------------------------------------------------------------------
// Architecture-Specific Context Layout
// -------------------------------------------------------------------------

#if defined(__XTENSA__)

/**
 * @struct TaskContext
 * @brief Xtensa register save area.
 *
 * Saves the 16 visible window registers (a0–a15), plus special registers:
 *   PS (processor state), SAR (shift amount), PC, LBEG, LEND, LCOUNT.
 * The window spill area handles overflow of the full 64-register file.
 */
struct TaskContext {
    u32 pc;             ///< Program counter (return address).
    u32 ps;             ///< Processor state register.
    u32 sar;            ///< Shift amount register.
    u32 lbeg;           ///< Loop begin address.
    u32 lend;           ///< Loop end address.
    u32 lcount;         ///< Loop counter.
    u32 a[16];          ///< Window registers a0–a15.
    u32 windowBase;     ///< WINDOWBASE special register.
    u32 windowStart;    ///< WINDOWSTART special register.
    u8  windowSpill[256]; ///< Spill area for register window overflow.
};

#elif defined(__riscv) && (__riscv_xlen == 32)

/**
 * @struct TaskContext
 * @brief RISC-V32 register save area.
 *
 * Saves x1 (ra) through x31, plus pc and mstatus.
 * x0 is hardwired zero and not saved.
 */
struct TaskContext {
    u32 pc;             ///< Program counter.
    u32 mstatus;        ///< Machine status register.
    u32 x[31];          ///< x1 (ra) through x31 (t6).
};

#elif defined(__x86_64__) || defined(_M_X64)

/**
 * @struct TaskContext
 * @brief x86_64 register save area.
 *
 * Saves all general-purpose registers, flags, and the SSE state.
 * The fxsave area must be 16-byte aligned.
 */
struct TaskContext {
    u64 rip;
    u64 rsp;
    u64 rbp;
    u64 rbx;
    u64 r12;
    u64 r13;
    u64 r14;
    u64 r15;
    u64 rflags;
    u64 rdi;
    u64 rsi;
    u64 rax;
    u64 rcx;
    u64 rdx;
    u64 r8;
    u64 r9;
    u64 r10;
    u64 r11;
    alignas(16) u8 fxsave[512]; ///< SSE/FPU state via fxsave/fxrstor.
};

#else

/**
 * @struct TaskContext
 * @brief Fallback stub for unsupported architectures.
 */
struct TaskContext {
    usz pc;
    usz sp;
    usz regs[32];
};

#endif

// -------------------------------------------------------------------------
// Context Switch API
// -------------------------------------------------------------------------

/**
 * @brief Saves the current CPU state into `from` and loads state from `to`.
 *
 * After this call returns (from the perspective of the `from` context),
 * the CPU will be executing the `to` context. When `from` is later
 * switched back to, execution resumes after this call.
 *
 * @param from  Pointer to the context to save current state into.
 * @param to    Pointer to the context to restore and jump to.
 */
extern "C" void xi_context_switch(TaskContext* from, TaskContext* to);

/**
 * @brief Initializes a new context so that when switched to, it begins
 *        executing `entry(arg)` with the given stack.
 *
 * @param ctx       Context to initialize.
 * @param entry     Entry point function.
 * @param arg       Argument passed to the entry function.
 * @param stack     Base of the allocated stack memory.
 * @param stackSize Size of the stack in bytes.
 */
void xi_context_init(TaskContext* ctx, void (*entry)(void*), void* arg,
                     u8* stack, usz stackSize);

/**
 * @brief Called when a task's entry function returns.
 *        Marks the task as finished and yields to the scheduler.
 */
extern "C" void xi_context_entry_trampoline(void* arg);

} // namespace Execution

#endif // XI_EXECUTION_CONTEXT_HPP
