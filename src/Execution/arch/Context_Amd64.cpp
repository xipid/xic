/**
 * @file Context_Amd64.cpp
 * @brief x86_64 context initialization and entry trampoline.
 *
 * Companion to Context_Amd64.S — provides the C++ functions
 * xi_context_init() for setting up a new task context.
 */

#if defined(__x86_64__) || defined(_M_X64)

#include "../../../include/Execution/Context.hpp"
#include "../../../include/Execution/Task.hpp"

#include <cstring>

namespace Execution {

/**
 * @brief Initializes an x86_64 TaskContext for a new task.
 *
 * Sets up:
 *   - rsp: top of stack, 16-byte aligned, with return address pushed
 *   - rip: xi_context_entry_trampoline
 *   - rdi: arg (System V ABI first argument register)
 *   - All other registers: zero
 *
 * The context switch restores rsp and jumps to rip. When the
 * trampoline is entered, rdi contains arg.
 */
void xi_context_init(TaskContext* ctx, void (*entry)(void*), void* arg,
                     u8* stack, usz stackSize) {
    std::memset(ctx, 0, sizeof(TaskContext));

    // Stack grows downward. Top of stack, 16-byte aligned.
    u64 stackTop = reinterpret_cast<u64>(stack + stackSize);
    stackTop &= ~0xFULL; // 16-byte align.

    // The System V ABI requires the stack to be 16-byte aligned
    // BEFORE the CALL instruction pushes the return address.
    // After push, rsp % 16 == 8. So we set rsp to stackTop - 8
    // to simulate the "just called" state.
    stackTop -= 8;

    ctx->rsp = stackTop;
    ctx->rip = reinterpret_cast<u64>(xi_context_entry_trampoline);
    ctx->rdi = reinterpret_cast<u64>(arg);

    // Store the actual entry function pointer in r12 (callee-saved)
    // so the trampoline can retrieve it. But actually, the trampoline
    // gets entry from TaskState, not from registers. So r12 is unused.
    // We store it anyway for debugging.
    ctx->r12 = reinterpret_cast<u64>(entry);

    // rbp = 0 (frame pointer — marks the bottom of the call chain).
    ctx->rbp = 0;

    // rflags: enable interrupts (IF=1), clear direction flag (DF=0).
    ctx->rflags = 0x200; // IF bit.
}

// Note: xi_context_entry_trampoline is defined in Task.cpp as a
// portable implementation. On x86_64, it's linked as extern "C".

} // namespace Execution

#endif // defined(__x86_64__) || defined(_M_X64)
