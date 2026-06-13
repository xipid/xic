# Software Fault Isolation (SFI) and Sandbox Containment

Security and isolation in the xic task subsystem do not rely on hardware memory management units (MMUs). Instead, isolation is achieved through Software Fault Isolation (SFI), Ahead-of-Time (AOT) binary rewriting, and strict scheduler-level validations.

## Software Fault Isolation (SFI) and AOT Rewriter

When binary code is loaded into an isolated task's executable memory region, the Ahead-of-Time (AOT) compiler parses the binary before execution. The rewriter instruments the instruction stream to ensure containment:

1. **Memory Access Instrumentation (Loads and Stores)**: Every instruction that reads from or writes to memory (e.g., `mov rax, [rbx]`) is preceded by a bounds-checking stub. This stub invokes `xi_sfi_bounds_check` (for reads) or `xi_sfi_write_check` (for writes). These functions check the target address against the task's allowed `MemoryRegion` list, trapping with a `ud2` instruction if a violation is detected.
   - **JIT Redundant Bounds Check Elimination (BCE)**: To minimize execution overhead and JIT code footprint, the rewriter employs static BCE. Consecutive stack/frame accesses (utilizing `RSP` or `RBP` base registers) within a basic block are tracked; if a memory access falls within a previously validated offset range (or can be proven safe via contiguous allocation boundaries), the bounds-checking stub is omitted.
2. **Control Flow Containment**:
   - **Direct Jumps and Calls**: Relative jumps and calls are resolved and rewritten to point to their corresponding offset in the patched code buffer. Jumps targeting addresses outside the AOT'd code buffer are blocked.
   - **Indirect Jumps and Calls**: Register-indirect jumps and calls (e.g., `jmp rax` or `call rbx`) and memory-indirect jumps/calls are unconditionally replaced with `ud2` traps to prevent dynamic control flow hijacks.
   - **Returns**: The `ret` instruction is replaced with a validated return stub (`emitRetCheckStub`). This stub reads the return address from the stack and validates it via `xi_sfi_jump_check` before executing the return, preventing stack-smashing attacks.

## Context Switch Validation

As a second line of defense (defense-in-depth), the scheduler executes `xi_validate_context_before_switch()` immediately before restoring a task's context onto a CPU core. This function performs the following checks:

- **Stack Pointer Validation**: The task's stack pointer (e.g., `RSP`) must reside strictly within its designated physical stack memory boundaries.
- **Instruction Pointer Validation**: The task's instruction pointer (e.g., `RIP`) must point to a valid executable memory region, a registered task entry function, or the kernel-managed entry trampoline.
- **Parent Chain Validation**: For non-isolated tasks, the instruction pointer is validated against the union of executable regions and AOT caches of its entire parent chain. This prevents a child task from jumping to arbitrary host code outside of its parent's scope.

If any validation check fails, the task's status is changed to `TaskStatus::Destroyed`, scheduling is aborted, and the CPU yields.

## Register Sanitization

When switching context from the kernel/scheduler to a task, volatile registers can leak sensitive data (such as kernel stack pointers, task registry structures, or cryptographic keys). To prevent this information leakage, the context switch assembly (`xi_context_switch` in `Context_Amd64.S`) zeroes all volatile registers (`rax`, `rcx`, `rdx`, `rsi`, `r8`, `r9`, `r10`, `r11`) before jumping to the task.

## Writable and Executable Memory (No W^X)

To support dynamic code generation (such as JIT compilation) within tasks, memory allocated via `alloc()` is mapped as both writable and executable.
The task subsystem relies on SFI and Ahead-of-Time (AOT) binary rewriting to instrument the instruction stream of any executable region (including dynamically written memory) before execution, ensuring that dynamic code cannot bypass sandbox containment boundaries.

## Instruction Hooking, Banning, and Prefix Bypass Prevention

Tasks can register custom callbacks or ban specific CPU instructions via the `onInstruction` and `offInstruction` APIs:
- **Callbacks**: A parent can register a callback on instructions like `cpuid` or `rdtsc`. The AOT rewriter replaces the target instruction with a trampoline that runs the callback.
- **Banning**: A parent can ban instructions, replacing them with `ud2` traps.
- **Self-Modification Prevention**: A task can register callbacks on its own instructions but is strictly blocked from unbanning an instruction that its parent or the kernel has banned.
- **Prefix Bypass Hardening**: To prevent an attacker from bypassing opcode-based bans by prepending instructions with prefixes (such as legacy override prefixes or REX prefixes, e.g., executing `REP syscall` instead of `syscall`), the AOT rewriter parses and skips all legacy and REX prefixes before performing instruction validation.
