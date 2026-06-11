# Task Subsystem API Reference

The `Execution::Task` class represents a handle to an execution state managed by the system scheduler.

## Static Management Helpers

### `static Task root()`
Returns a handle to the root kernel task (ID 0).

### `static Task current()`
Returns a handle to the currently executing task on the active CPU core.

### `static Task findTask(usz id)`
Finds a task in the system registry by its ID. Returns an invalid task handle if the ID does not exist.

### `static usz taskCount()`
Returns the total number of tasks currently registered in the system.

### `static void enable(usz coreId)`
Enables task scheduling and periodic timer interrupts on the specified CPU core.

### `static void disable(usz coreId)`
Stops scheduling on the specified CPU core and migrates all unpinned tasks to other enabled cores.

### `static void setFrequencySlider(usz coreId, u32 minFreq, u32 maxFreq)`
Configures the frequency limits for the dynamic frequency governor on the specified core.

---

## Properties and Accessors

### `usz id() const`
Returns the unique task ID. Returns 0 for the root kernel task.

### `usz parentId() const`
Returns the ID of the parent task that spawned this task.

### `TaskStatus status() const`
Returns the current execution status of the task.

### `bool valid() const`
Returns true if this task handle points to a valid internal task state.

### `usz getStackSize() const`
Returns the allocated stack size of the task in bytes.

### `bool isPinned() const`
Returns true if the task is pinned to a specific CPU core.

### `usz childCount() const`
Returns the number of child tasks currently active under this task.

### `Task child(usz index)`
Returns a task handle to the child task at the specified index.

---

## Lifecycle Management

### `Task spawn()`
Spawns a new child task with the caller set as the parent. The child begins in the `Created` state.

### `template <typename Fn, typename... Args> Task spawn(Fn fn, Args... args)`
Spawns a child task and initializes its context to execute `fn(args...)`. The task is automatically transitioned to `Ready` and enqueued for scheduling.

### `template <typename Fn, typename... Args> Task async(Fn fn, Args... args)`
Spawns a child task, serializes the arguments into its inbox, and sets up a trampoline to invoke the function.

### `void resume()`
Transitions the task status to `Ready` and enqueues it. If no memory regions have been allocated, automatically maps a default stack region (4KB).

### `void stop()`
Indefinitely pauses the task. If the task is currently running, yields execution.

### `void stop(u64 us)`
Puts the task to sleep for the specified duration in microseconds.

### `void destroy()`
Destroys the task and recursively destroys all its descendant child tasks. All allocated resources, memory regions, and AOT caches are freed.

---

## Context and Jumps

### `void jump(usz addr)`
Sets the task's program counter to the specified virtual address. Invalidates AOT caches for the region and enqueues the task.

### `template <typename Fn, typename... Args> void jump(Fn fn, Args... args)`
Type-safe jump. Initializes the task stack and starts execution of `fn(args...)`.

### `void yield(usz coreId = 0)`
Cooperatively yields the remaining time slice on the active core. If called inside a task, the task saves its remaining quota for the next scheduling period.

### `void wait()`
Suspends the current task and flags it as waiting for a message. Yields the CPU immediately.

### `void wait(usz addr)`
Suspends the task and registers its resume address at `addr`.

### `template <typename Fn, typename... Args> void wait(Fn fn, Args... args)`
Suspends the current task and registers the function `fn(args...)` to execute upon resumption.

---

## Memory and Sandboxing

### `void alloc(usz dest, usz length)`
Allocates physical memory of `length` bytes and maps it to the task's virtual address space starting at `dest`. Maps the region as both writable and executable.

### `void map(usz source, usz dest, usz length)`
Maps virtual memory from the caller's space (`source`) into the target task's virtual space (`dest`). Mapped memory is read-only.

### `void unmap(usz dest, usz length)`
Unmaps virtual memory regions overlapping with the range `[dest, dest + length)`.

### `void unmap()`
Clears all memory regions, mappings, memory translation buffers, and fetch callbacks. The task is transitioned to Full Isolation mode (`isIsolated = true`), restricting it strictly to its own address space starting at 0.

### `void translate(const MemoryTranslation& mt)`
Adds a custom address translation rule to the task.

### `void copy(usz source, usz dest, usz length)`
Copies bytes between two virtual addresses within the task's memory map.

### `void setOnFetch(Func<void(usz dest, usz length)> cb)`
Sets a callback triggered when the task attempts to fetch memory from an unmapped region.

### `void onInstruction(const String& instruction, Func<void()> callback)`
Registers a hook to intercept specific CPU instructions. When the AOT compiler encounters the instruction in the task's code, it inserts a branch to the registered callback.

### `void offInstruction(const String& instruction)`
Bans the execution of a specific instruction. If encountered, the task is terminated.

---

## Inter-Process Communication (IPC)

### `void send(Task& receiver, const String& payload)`
Sends a message containing `payload` to the receiver task's inbox.
- **Security Check**: Sandboxed child tasks can only send messages to siblings, child tasks, or parent tasks.
- **Auto-Share**: The sender's task handle is automatically shared with the receiver.
- **Waking**: If the receiver is suspended waiting for a message, it is marked `Ready` and enqueued.

### `void share(Task& taskObj)`
Grants the task read-only access to `taskObj`'s state and memory map.

### `Array<Message>& inbox()`
Exposes the task's incoming message queue.

### `Array<String>& log()`
Exposes the task's execution and logging stream.

---

## Quotas and Scheduling

### `void setQuota(u64 us)`
Sets the maximum execution quota (in microseconds) per scheduling period (default: 10ms). A quota of 0 denotes unlimited background execution.

### `void setMemorySize(usz bytes)`
Overrides the default memory allocation size. Must be called before `resume()` or any manual `alloc()`.

### `void setMinChildQuota(u64 us)`
Sets the minimum quota that any child task spawned by this task must be allocated. Used for fork bomb protection.
