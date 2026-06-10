# Unified Tasks

The **Execution::Task** subsystem is the fundamental unit of execution in the `xic` kernel. It unifies the traditional, fragmented OS concepts of **processes**, **threads**, **coroutines**, and **functions** into a single, unified execution abstraction.

By combining **Software Fault Isolation (SFI)** with **Ahead-of-Time (AOT)** binary rewriting and dynamic, demand-based scheduling, `Execution::Task` allows you to run isolated, sandboxed containers with the overhead and speed of user-space threads—even on bare-metal microcontrollers without physical Memory Management Units (MMUs).

---

## 💎 Core Philosophy

1. **Unified Execution Model**: A task is simply a lightweight handle to an execution state. The parent can customize the task's address translation maps, memory access permissions, CPU quota allocation, and hardware instruction rules before resuming it.
2. **MMU-Less Sandboxing**: Security and isolation do not require hardware-level page tables. The compiler/loader rewrites load/store/jump instructions (SFI) and the scheduler validates context bounds (`xi_validate_context_before_switch`) before every thread switch.
3. **Proactive Power Governance**: CPU clock speeds are adjusted immediately based on the calculated microsecond quotas of active tasks, rather than waiting for reactive CPU utilization samples.

---

## 📦 Data Types & Enums

### TaskStatus

Represents the current execution state of a task.

```cpp
enum class TaskStatus : u8 {
    Created,    ///< Allocated but never started.
    Running,    ///< Currently executing on a core.
    Ready,      ///< Runnable, waiting in the run queue.
    Paused,     ///< Explicitly paused (cooperatively or via stop()).
    Sleeping,   ///< Paused until a specific system timestamp.
    Finished,   ///< Entry function returned.
    Destroyed   ///< Marked for cleanup.
};
```

---

### MemoryRegion

Describes a mapped virtual region in the task's address space.

```cpp
struct MemoryRegion {
    usz base;           ///< Virtual base address in the task's space.
    usz size;           ///< Size in bytes.
    u8* physical;       ///< Backing physical memory pointer.
    bool writable;      ///< True if task can write to this region.
    bool executable;    ///< True if task can execute from this region.
    bool owned;         ///< If true, physical memory is freed when unmapped.
};
```

---

### MemoryTranslation

Specifies an address translation rule from a task's virtual space to a physical or another task's space.

```cpp
struct MemoryTranslation {
    usz source;         ///< Source address (in the task's virtual space).
    usz dest;           ///< Destination address (physical or another task's space).
    usz validFor;       ///< Number of bytes this translation covers.
};
```

---

### Message

Represents an Inter-Process Communication (IPC) message payload.

```cpp
struct Message {
    usz senderId;       ///< ID of the sending task.
    String payload;     ///< Arbitrary binary or string payload.
};
```

---

### CoreState

Per-core scheduling and frequency management state.

```cpp
struct CoreState {
    usz id;                     ///< Core ID.
    bool enabled;               ///< True if core is managed by the scheduler.
    u32 minFreq;                ///< Minimum proposed clock frequency (Hz).
    u32 maxFreq;                ///< Maximum proposed clock frequency (Hz).
    u32 currentProposedFreq;    ///< Last proposed clock frequency.
    Array<usz> runQueue;        ///< Queue of active Task IDs.
    usz currentTaskId;          ///< ID of currently executing task (0 = idle).
    usz runQueueIndex;          ///< Current position in round-robin scheduling.
    u64 totalQuotaUs;           ///< Sum of all task quotas active on this core.
    TaskContext idleContext;    ///< Context to return to when idle.
};
```

---

## 🛠️ API Reference

### Static Management Helpers

#### `static Task root()`
Returns a handle to the root kernel task (ID `0`).

#### `static Task current()`
Returns a handle to the currently executing task on the active CPU core.

#### `static Task findTask(usz id)`
Finds a task in the system registry by its ID. Returns an invalid task handle if not found.

#### `static usz taskCount()`
Returns the total number of tasks currently registered in the system.

#### `static void enable(usz coreId)`
Enables task scheduling and periodic timer interrupts on the specified CPU core.

#### `static void disable(usz coreId)`
Stops scheduling on the specified core and migrates all unpinned tasks to other enabled cores.

#### `static void setFrequencySlider(usz coreId, u32 minFreq, u32 maxFreq)`
Sets the frequency limits for the dynamic frequency governor on a core.

---

### Properties & Accessors

#### `usz id() const`
Returns the unique task ID. Returns `0` for the root kernel task.

#### `usz parentId() const`
Returns the ID of the parent task that spawned this task.

#### `TaskStatus status() const`
Returns the task's current execution status (e.g., Running, Paused, Ready).

#### `bool valid() const`
Returns `true` if this task handle points to a valid internal task state.

#### `usz getStackSize() const`
Returns the allocated stack size of the task in bytes. If a custom stack was not allocated, returns the default size (4KB) or the largest mapped region.

#### `bool isPinned() const`
Returns `true` if the task is pinned to a specific CPU core.

#### `usz childCount() const`
Returns the number of child tasks currently active under this task.

#### `Task child(usz index)`
Returns a `Task` handle to the child task at the specified index.

---

### Lifecycle Management

#### `Task spawn()`
Spawns a new child task with the current task set as the parent. The child begins in `TaskStatus::Created`.

#### `template <typename Fn, typename... Args> Task spawn(Fn fn, Args... args)`
Spawns a child task and initializes it to execute `fn(args...)`. The task is automatically started/enqueued.

#### `template <typename Fn, typename... Args> Task async(Fn fn, Args... args)`
Spawns a child task, serializes the arguments into its inbox, and sets up a trampoline to invoke the function.

#### `void resume()`
Starts or resumes execution of the task. If no memory regions have been allocated, automatically maps a default stack region (4KB).

#### `void stop()`
Indefinitely pauses the task. If the task is currently running, yields execution.

#### `void stop(u64 us)`
Puts the task to sleep for the specified duration in microseconds.

#### `void destroy()`
Destroys the task and recursively destroys all its descendant child tasks. All allocated resources and memory maps are freed.

---

### Context & Jumps

#### `void jump(usz addr)`
Sets the task's program counter to a virtual address. Invalidates AOT caches for the region and enqueues the task.

#### `template <typename Fn, typename... Args> void jump(Fn fn, Args... args)`
Type-safe jump. Forces the task stack to initialize and begins execution of `fn(args...)`.

#### `void yield(usz coreId = 0)`
Cooperatively yields the remaining time slice on the active core. If called inside a task, the task saves its remaining quota for the next scheduling period.

#### `void wait()`
Puts the current task into `TaskStatus::Paused` and flags it as waiting for a message. Yields the core immediately.

#### `void wait(usz addr)`
Suspends the task and registers its resume address at `addr`.

#### `template <typename Fn, typename... Args> void wait(Fn fn, Args... args)`
Suspends the current task and registers the function `fn(args...)` to execute upon resumption.

---

### Memory & Sandboxing

#### `void alloc(usz dest, usz length)`
Allocates physical memory of `length` bytes and maps it to the task's virtual address space starting at `dest`.

#### `void map(usz source, usz dest, usz length)`
Maps virtual memory from the caller's space (`source`) into the target task's virtual space (`dest`). Mapped memory is read-only by default.

#### `void unmap(usz dest, usz length)`
Unmaps any virtual memory regions overlapping with the range `[dest, dest + length)`.

#### `void unmap()`
Clears all memory regions, mappings, memory translation buffers, and fetch callbacks. The task is transitioned to **Full Isolation mode** (`isIsolated = true`), restricting it strictly to its own address space starting at `0`.

#### `void translate(const MemoryTranslation& mt)`
Adds a custom address translation rule to the task.

#### `void copy(usz source, usz dest, usz length)`
Copies bytes between two virtual addresses within the task's memory map, handling address boundary gaps.

---

### Inter-Process Communication (IPC)

#### `void send(Task& receiver, const String& payload)`
Sends a message containing `payload` to the receiver task's inbox.
* **Security**: If the caller is a sandboxed child, the receiver must be a sibling, child, or parent task. Escalations outside the container boundary are blocked.
* **Auto-Share**: The sender's task handle is automatically shared to the receiver to allow reply messages.
* **Waking**: If the receiver is suspended waiting for a message, it is marked `TaskStatus::Ready` and enqueued.

#### `void share(Task& taskObj)`
Grants the task read-only access to `taskObj`'s state and memory map.

#### `Array<Message>& inbox()`
Exposes the task's incoming message queue.

#### `Array<String>& log()`
Exposes the task's execution/logging stream. By convention, the last entry in the log serves as the return value.

---

### Quotas & Scheduling

#### `void setQuota(u64 us)`
Sets the maximum execution quota (in microseconds) per scheduling period (default: 10ms). A quota of `0` denotes unlimited background execution.

#### `void setMemorySize(usz bytes)`
Overrides the default memory allocation size. Must be called before `resume()` or any manual `alloc()`.

---

### Callbacks & Hooks

#### `void setOnFetch(Func<void(usz dest, usz length)> cb)`
Sets a callback triggered when the task attempts to fetch memory from an unmapped or unpatched region.

#### `void onInstruction(const String& instruction, Func<void()> callback)`
Registers a hook to intercept specific CPU instructions. When the AOT compiler encounters the instruction in the task's code, it inserts a branch to the registered callback.

#### `void offInstruction(const String& instruction)`
Bans the execution of a specific instruction. If encountered, the task is terminated.

---

## 🔒 Security & Sandboxing (Under the Hood)

The task subsystem relies on a defense-in-depth model that combines software isolation with kernel-level checks:

### 1. Context Switch Validation
Before a context switch is performed on a hardware core, the scheduler runs `xi_validate_context_before_switch` to verify the incoming context:
* The Stack Pointer (`SP`/`RSP`) must reside strictly within the task's physical stack memory bounds.
* The Instruction Pointer (`IP`/`RIP`/`PC`) must point to a kernel-managed trampoline, the registered task entry function, or an explicitly marked executable memory region.
* If a violation is detected, the status is set to `TaskStatus::Destroyed` and the task is dropped.

### 2. Software Fault Isolation (SFI)
When code is loaded into an isolated task, the **AOT (Ahead-of-Time)** rewriter ([AOT.hpp](file:///home/xi/Repo/xic/include/Execution/AOT.hpp)) scans the binary:
* All memory reads and writes (loads and stores) are patched with inline bounds-checks against the task's `MemoryRegion` map.
* Jumps and calls are modified to prevent escaping the task's instruction regions.
* Banned instructions (e.g. `syscall`, `sysenter`, or hardware peripheral instructions) are rewritten to trigger faults or branch to custom callbacks registered via `onInstruction`.

---

## ⚡ Dynamic Clock Speed Scaling (DVFS)

The scheduler implements proactive power management to keep CPUs cool:
1. Every core tracks the sum of all active task quotas (`totalQuotaUs`).
2. When scheduling, `proposeFrequency` calculates the required compute demand:
   * **Zero Tasks Ready**: Immediately dials frequency down to `minFreq`.
   * **Full Demand**: If cumulative quotas meet or exceed the scheduling period, frequency is set to `maxFreq`.
   * **Interpolated Demand**: Frequency is set proportionally:
     $$\text{proposed} = f_{\text{min}} + \frac{\text{Demand} \times (f_{\text{max}} - f_{\text{min}})}{\text{Period}}$$
3. The platform-specific hardware governor is notified via `onChangeFrequency` to adjust voltages and frequencies instantly.

---

## 💻 Code Examples

### 1. Basic Task Spawning

```cpp
#include <cstdio>
#include "Execution/Task.hpp"

using namespace Execution;

void worker(void* arg) {
    int count = *static_cast<int*>(arg);
    std::printf("Hello from Task! Arg count: %d\n", count);
}

int main() {
    Task root = Task::root();
    Task child = root.spawn();

    int val = 42;
    child.jump(worker, &val); // Spawns, initializes, and starts child task.

    // Enable core 0 and drive execution
    Task::enable(0);
    Task::yield(0); 

    child.destroy();
    return 0;
}
```

### 2. Sandbox Creation & Full Memory Isolation

```cpp
#include "Execution/Task.hpp"

using namespace Execution;

void untrusted_code(void* arg) {
    // Attempting to access parent memory directly will fault
    // as it is running in full isolation mode.
    volatile int* illegal_ptr = (int*)0x7FFF0000;
    *illegal_ptr = 99;
}

int main() {
    Task root = Task::root();
    Task sandbox = root.spawn();

    // Isolate the sandbox — clears all parent memory inheritance
    sandbox.unmap();

    // Allocate isolated memory starting at virtual address 0
    sandbox.alloc(0, 4096);

    sandbox.jump(untrusted_code, nullptr);
    sandbox.resume();

    Task::enable(0);
    Task::yield(0);

    return 0;
}
```

### 3. IPC Message Passing (Wait and Wake)

```cpp
#include <cstdio>
#include "Execution/Task.hpp"

using namespace Execution;

void listener(void* arg) {
    Task self = Task::current();
    std::printf("Listener task waiting for message...\n");
    self.wait(); // Suspends until a message arrives in inbox

    if (self.inbox().size() > 0) {
        std::printf("Message received: %s\n", self.inbox()[0].payload.c_str());
    }
}

int main() {
    Task::enable(0);
    Task root = Task::root();
    Task child = root.spawn(listener, nullptr); // Automatically starts

    Task::yield(0); // Let the listener suspend

    // Send wakeup message
    root.send(child, "Activate!");

    Task::yield(0); // Run listener again

    child.destroy();
    return 0;
}
```
