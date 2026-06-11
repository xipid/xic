# The Architecture of the Unified Task Subsystem

The Execution::Task subsystem is the fundamental execution unit in the xic kernel. It unifies traditional, separate operating system primitives—such as processes, threads, coroutines, and functions—into a single unified abstraction.

## Unified Abstraction Model

In traditional operating systems, execution is split across multiple layers:
- **Processes** provide heavy-weight virtual memory isolation boundaries.
- **Threads** provide concurrent paths of execution sharing a process address space.
- **Coroutines** provide user-space cooperative scheduling.

This fragmentation introduces significant runtime overhead and complexity. In xic, this hierarchy is collapsed into a single class: `Execution::Task`. A task is represented internally by a lightweight `TaskState` structure. The nature of a task depends entirely on its configuration:
- **Non-isolated Tasks** share the parent task's memory space and context, functioning like lightweight threads or coroutines.
- **Isolated Tasks** clear all inherited parent memory mappings. They run within their own sandboxed memory region starting at virtual address 0, functioning like isolated processes.
- **Cooperative Tasks** explicitly yield control back to the scheduler, functioning like coroutines.

This unified model allows applications to scale down to bare-metal microcontrollers (such as the ESP32) while retaining the structural benefits of process isolation and thread-level concurrency.

## Task States and Lifecycle

The lifecycle of a task is defined by the `TaskStatus` enumeration:

```cpp
enum class TaskStatus : u8 {
    Created,    // Allocated but never started.
    Running,    // Currently executing on a CPU core.
    Ready,      // Runnable and waiting in the scheduler's run queue.
    Paused,     // Suspended (cooperatively or via scheduler intervention).
    Sleeping,   // Paused until a specific system timestamp is reached.
    Finished,   // The task's entry function has returned.
    Destroyed   // Marked for cleanup.
};
```

1. **Creation**: When `spawn()` is called, a `TaskState` is allocated and assigned a unique ID. It starts in the `Created` state.
2. **Initialization**: The task's stack and context are initialized either implicitly during the first call to `resume()` or explicitly via a call to `jump()`.
3. **Execution**: The task is placed in the `Ready` queue of an enabled CPU core. The scheduler schedules it using a round-robin model driven by periodic timer interrupts.
4. **Suspension**: A task can transition to `Paused` by calling `stop()`, or `Sleeping` by calling `stop(us)`.
5. **Termination**: Once the entry function returns, the task enters `Finished`. If the parent task calls `destroy()`, the task and all of its descendants are transitioned to `Destroyed` and their resources are reclaimed.

## Memory Model and Parent-Child Hierarchy

Memory allocation in the task subsystem operates on a top-down security model:
- **Parental Sovereignty**: A parent task has full read and write access to its children's memory spaces. The security boundary is enforced from the top down, meaning a parent can monitor, modify, and terminate any child context.
- **Memory Carving**: To prevent child tasks from consuming arbitrary host memory (which could lead to resource exhaustion or fork-bomb attacks), child allocations are carved directly out of the parent's memory regions.
- **Fallback Allocation**: If a parent task does not have pre-allocated memory regions, the allocator dynamically allocates a fresh heap block, registers it in the parent's memory map, and then carves the child's memory from it. This ensures that a parent always retains visibility and ownership of its children's physical memory.
