/**
 * @file Task.hpp
 * @brief Task abstraction — the fundamental unit of execution in the Task subsystem.
 *
 * A Task unifies processes, threads, and functions under a single concept.
 * Tasks form a parent-child tree. Each task has its own inbox (IPC),
 * log (output), memory map, and scheduling quota.
 *
 * Convention:
 *   - inbox[0] is the argument list (set by the parent before start).
 *   - log[log.size()-1] is the return value.
 *   - The parent can read all child memory and control child execution.
 *   - A sender is automatically shared to the receiver on message send.
 */

#ifndef XI_EXECUTION_TASK_HPP
#define XI_EXECUTION_TASK_HPP

#include "../Collection/Array.hpp"
#include "../Collection/String.hpp"
#include "../Xi/Func.hpp"
#include "AOT.hpp"
#include "Context.hpp"
#include <cstdlib>

/// Default memory allocation for a task if alloc() is not called before start().
static constexpr usz XI_DEFAULT_TASK_MEM = 4096;

namespace Execution {

using namespace Xi;
using namespace Collection;

#define XI_DECLARE_EXECUTION_OVERLOADS(name, mode, targetId) \
    template <typename Fn, typename... Args> \
    void name(Fn fn, Args... args) { \
        _execute_impl(mode, targetId, fn, args...); \
    } \
    void name(void (*fn)(void*), void* arg = nullptr) { \
        _execute_impl_raw(mode, targetId, fn, arg); \
    } \
    void name(usz addr) { \
        _execute_impl_raw(mode, targetId, (void(*)(void*))addr, nullptr); \
    } \
    void name() { \
        _execute_impl_raw(mode, targetId, nullptr, nullptr); \
    }

struct TaskState;

// Thread-local current task accessors (defined in Task.cpp)
TaskState* xi_get_current_task();
void xi_set_current_task(TaskState* s);
bool xi_validate_context_before_switch(TaskState* state);
bool xi_is_task_dead_recursive(usz tid);

extern thread_local usz xi_last_guest_rbx;

struct GuestRegs {
    u64 r11;
    u64 r10;
    u64 r9;
    u64 r8;
    u64 rdi;
    u64 rsi;
    u64 rdx;
    u64 rcx;
    u64 rbx;
    u64 rax;
    u64 rflags;
};

extern thread_local GuestRegs* xi_guest_regs;

// -------------------------------------------------------------------------
// Supporting Structures
// -------------------------------------------------------------------------

/**
 * @struct MemoryRegion
 * @brief Describes a mapped region in a task's virtual address space.
 */
struct MemoryRegion {
    usz base;           ///< Virtual base address in the task's space.
    usz size;           ///< Size in bytes.
    u8* physical;       ///< Backing physical memory pointer (owned or mapped).
    bool writable;      ///< Whether the task can write to this region.
    bool executable;    ///< Whether the task can execute from this region.
    bool owned;         ///< If true, physical memory is freed on unmap/destroy.
};

/**
 * @struct MemoryTranslation
 * @brief A source-to-destination address translation entry.
 */
struct MemoryTranslation {
    usz source;         ///< Source address (in the task's virtual space).
    usz dest;           ///< Destination address (physical or another task's space).
    usz validFor;       ///< How many bytes this translation covers.
};

/**
 * @struct Message
 * @brief An IPC message from one task to another.
 */
struct Message {
    usz senderId;       ///< ID of the sending task.
    String payload;     ///< Arbitrary byte payload.
};

// -------------------------------------------------------------------------
// Task State (internal)
// -------------------------------------------------------------------------

/**
 * @enum TaskStatus
 * @brief Execution state of a task.
 */
enum class TaskStatus : u8 {
    Created,    ///< Allocated but never started.
    Running,    ///< Currently executing on a core.
    Ready,      ///< Runnable, waiting for scheduler.
    Paused,     ///< Explicitly paused (via stop()).
    Sleeping,   ///< Paused until a specific time.
    Finished,   ///< Entry function returned.
    Destroyed   ///< Marked for cleanup.
};

/**
 * @struct TaskState
 * @brief Internal state managed by Task. Not exposed to user code.
 */
struct TaskState {
    usz id;
    usz parentId;

    TaskStatus status;
    TaskContext context;

    // Scheduling
    u64 quotaUs;            ///< Quota in microseconds per scheduling period. 0 = unlimited.
    u64 remainingUs;        ///< Remaining slice in the current period.
    u64 sleepUntilUs;       ///< For Sleeping status: wake-up time in system micros.
    usz pinnedCore;         ///< Core to pin to (only used if isPinned == true).
    bool isPinned;
    usz currentCore;        ///< Core this task is currently assigned to.

    // Stack
    u8* stack;              ///< Allocated stack memory.
    usz stackSize;          ///< Stack size in bytes.
    bool stackOwned;        ///< Whether we own (should free) the stack.

    // Memory
    Array<MemoryRegion> regions;
    Array<MemoryTranslation> translations;
    Array<AOTRegion> aotCache;

    // IPC
    Array<Message> inbox;
    Array<String> log;

    // Hierarchy
    Array<usz> childIds;
    Array<usz> sharedIds;   ///< IDs of tasks shared to this one.

    // Callbacks (stored here, proxied through Task handle)
    struct FetchRange {
        usz start;
        usz end;
        Func<void(usz start, usz end)> callback;
        bool cached;
        bool resolved;
    };
    Array<FetchRange> fetchRanges;

    struct CopyMapRegion {
        usz dest;
        usz size;
    };
    Array<CopyMapRegion> copyMapCache;

    // Entry point for raw tasks
    void (*entryFn)(void*);
    void* entryArg;
    bool isWaitingForMessage;
    bool isIsolated;        ///< If true, task is memory-isolated (sees own address space from 0).
    usz waitDeadTarget;     ///< Target task ID to wait for death (0 = none, -1 = all, -2 = children).

    // Fork bomb protection: minimum quota each child inherits, subtracted from parent.
    // If parent quota is limited and insufficient for minChildQuotaUs, spawn fails.
    u64 minChildQuotaUs;    ///< Min quota forced on children. 0 = inherit parent default.
    u64 childQuotaUsed;     ///< Sum of quota allocated to living children (when parent quota > 0).

    struct InstructionHook {
        String name;
        Func<void()> callback;
        bool banned;
    };
    Array<InstructionHook> instructionHooks;

    struct InstructionTranslator {
        String name;
        Func<Array<u8>(const Array<u8>&)> callback;
    };
    Array<InstructionTranslator> instructionTranslators;

    Array<String> bannedList;

    usz maxChildrenMemory;
    Func<void(usz, usz)> swapCallback;

    struct StoreRange {
        usz start;
        usz end;
        Func<void(usz, usz)> callback;
    };
    Array<StoreRange> storeRanges;

    Func<void()> customSyscallCallback;

    TaskState()
        : id(0), parentId(0), status(TaskStatus::Created),
          context{}, quotaUs(0), remainingUs(0), sleepUntilUs(0),
          pinnedCore(0), isPinned(false), currentCore(0),
          stack(nullptr), stackSize(0), stackOwned(false),
          isWaitingForMessage(false), isIsolated(false),
          entryFn(nullptr), entryArg(nullptr), waitDeadTarget(0),
          minChildQuotaUs(0), childQuotaUsed(0),
          maxChildrenMemory(0) {
        bannedList.push("syscall");
        bannedList.push("sysenter");
        bannedList.push("sysexit");
        bannedList.push("sysret");
        bannedList.push("int");
        bannedList.push("int3");
        bannedList.push("hlt");
        bannedList.push("cli");
        bannedList.push("sti");
        bannedList.push("wrmsr");
        bannedList.push("rdmsr");
        bannedList.push("iret");
        bannedList.push("mov-seg");
        bannedList.push("lss");
        bannedList.push("fsgsbase");
        bannedList.push("call-far");
        bannedList.push("jmp-far");
        bannedList.push("sysctl");
    }
};

/**
 * @struct CoreState
 * @brief Per-core scheduling state.
 */
struct CoreState {
    usz id;                     ///< Core ID.
    bool enabled;               ///< Whether this core is managed by Task.
    u32 minFreq;                ///< Minimum proposed frequency (Hz). 0 = no control.
    u32 maxFreq;                ///< Maximum proposed frequency (Hz). 0 = no control.
    u32 currentProposedFreq;    ///< Last proposed frequency.
    Array<usz> runQueue;        ///< Task IDs in the run queue for this core.
    usz currentTaskId;          ///< ID of the currently executing task (0 = idle).
    usz runQueueIndex;          ///< Current position in round-robin.
    u64 totalQuotaUs;           ///< Sum of quotas of all tasks on this core.
    TaskContext idleContext;    ///< Context to return to when no tasks are ready.

    CoreState()
        : id(0), enabled(false), minFreq(0), maxFreq(0),
          currentProposedFreq(0), currentTaskId(0), runQueueIndex(0),
          totalQuotaUs(0), idleContext{} {}
};

// -------------------------------------------------------------------------
// Task Handle
// -------------------------------------------------------------------------

/**
 * @class Task
 * @brief User-facing handle to a scheduled task.
 *
 * A Task is a lightweight handle (pointer to internal TaskState).
 * Copying a Task copies the handle, not the task itself.
 *
 * Tasks are always created via Task::root() or via an existing Task's
 * spawn()/async() methods.
 */
class XI_EXPORT Task {
public:
    TaskState* _state;  ///< Internal state.

    // Global Scheduling State (Static)
    static Array<TaskState*> _tasks;
    static Array<CoreState> _cores;
    static usz _nextTaskId;
    static u64 _schedulePeriodUs;
    static u32 _interruptIntervalUs;
    static Func<void(usz, u32)> _onChangeFrequency;
    static Task* instance;
    
    struct PhysicalAllocation {
        u8* ptr;
        usz size;
        usz refCount;
        bool isMmap;
    };
    static Array<PhysicalAllocation> _allocations;

    static void ensureInitialized();
    static Task root();
    static Task findTask(usz id);
    static usz taskCount();

    static void registerAllocation(u8* ptr, usz size, bool isMmap = false);
    static void retainAllocation(u8* ptr);
    static void releaseAllocation(u8* ptr);

    static TaskState* allocTask(usz parentId);
    static void enqueue(usz taskId);
    static void dequeue(usz taskId);
    static void destroyTask(usz taskId);
    static CoreState* coreState(usz coreId);
    static TaskState* currentTask(usz coreId);
    static TaskState* pickNext(usz coreId);
    static void proposeFrequency(usz coreId);
    static usz assignCore(usz taskId);
    static void resetPeriod(usz coreId);

    struct OnChangeFrequencyProxy {
        Task* parent;
        void operator=(Func<void(usz, u32)> cb);
        operator bool() const;
        void operator()(usz coreId, u32 proposedFreq) const;
    };
    OnChangeFrequencyProxy onChangeFrequency;

    Task() : _state(nullptr), onChangeFrequency{this} {}
    Task(TaskState* state) : _state(state), onChangeFrequency{this} {}
    Task(const Task& other) : _state(other._state), onChangeFrequency{this} {}
    Task(Task&& other) noexcept : _state(other._state), onChangeFrequency{this} {}
    Task& operator=(const Task& other) {
        _state = other._state;
        return *this;
    }
    ~Task() = default;

    // -- Identity --

    /** @brief Returns the task ID. */
    usz id() const { return _state ? _state->id : 0; }

    /** @brief Returns the parent task ID. */
    usz parentId() const { return _state ? _state->parentId : 0; }

    /** @brief Returns the parent task. */
    Task parent() const { return findTask(parentId()); }

    /** @brief Returns the current status. */
    TaskStatus status() const {
        return _state ? _state->status : TaskStatus::Destroyed;
    }

    /** @brief Returns true if the task handle is valid. */
    bool valid() const { return _state != nullptr; }

    /** @brief Dynamic helper returning active stack size or querying mapped regions. */
    usz getStackSize() const {
        if (!_state) return 0;
        if (_state->stackSize > 0) return _state->stackSize;
        usz maxReg = 0;
        for (usz i = 0; i < _state->regions.size(); ++i) {
            if (_state->regions[i].size > maxReg) {
                maxReg = _state->regions[i].size;
            }
        }
        usz size = maxReg > 0 ? maxReg : XI_DEFAULT_TASK_MEM;
        return size < XI_DEFAULT_TASK_MEM ? XI_DEFAULT_TASK_MEM : size;
    }

    /** @brief Returns a handle to the currently executing task on this core. */
    static Task current();

    // -- Core Management --
    static void setup(usz coreId, bool startTimer = true);
    static void disable(usz coreId);
    static void setFrequencySlider(usz coreId, u32 minFreq, u32 maxFreq);

    // -- Core Affinity --

    /** @brief Pins this task to a specific core. */
    void setPin(usz coreId);

    /** @brief Unpins this task (allows migration). */
    void clearPin();

    /** @brief Returns whether this task is pinned. */
    bool isPinned() const { return _state ? _state->isPinned : false; }

    // -- Lifecycle --

    /**
     * @brief Spawns a new child task.
     *
     * The child's parent is this task. The child starts in Created status.
     */
    Task spawn();

    /**
     * @brief Spawns a child task under this task, initializing it to execute fn(args...).
     *        The task is automatically started/enqueued.
     */
    template <typename Fn, typename... Args>
    Task spawn(Fn fn, Args... args) {
        Task child = spawn();
        if (child.valid()) {
            child.jump(fn, args...);
        }
        return child;
    }



    /**
     * @brief Spawns a child task wrapping a C++ function.
     *
     * The wrapper:
     *   1. On first start, shifts args from inbox into the C++ parameter list.
     *   2. Calls the function.
     *   3. Pushes the return value into log.
     */
    template <typename Fn, typename... Args>
    Task async(Fn fn, Args... args);

    XI_DECLARE_EXECUTION_OVERLOADS(jump, 0, 0)
    XI_DECLARE_EXECUTION_OVERLOADS(wait, 1, 0)
    XI_DECLARE_EXECUTION_OVERLOADS(waitDead, 2, (usz)-2)

    /**
     * @brief Starts or resumes execution.
     *
     * If no memory has been allocated via alloc(), automatically allocates
     * XI_DEFAULT_TASK_MEM (4KB) at virtual address 0 for the task to use
     * as stack, heap, or however it sees fit.
     */
    void resume();

    /** @brief Pauses execution indefinitely (cooperative yield). */
    void stop();

    /** @brief Pauses execution for the given number of microseconds. */
    void stop(u64 us);



    /** @brief Destroys this task and all its children. Frees all memory. */
    void destroy();

    /** @brief Cooperatively yields execution time on the specified core. */
    static void yield(usz coreId = 0);

    /** @brief Hook an instruction execution. */
    void onInstruction(const String& instruction, Func<void()> callback);

    /** @brief Ban an instruction and remove callback. */
    void offInstruction(const String& instruction);

    void onInstructionTranslate(const String& instruction, Func<Array<u8>(const Array<u8>&)> callback);
    void forwardInstruction(const String& instruction);
    void setOnSwap(Func<void(usz, usz)> cb);
    void setOnStore(Func<void(usz, usz)> cb);
    void setOnStore(usz start, usz end, Func<void(usz, usz)> cb);
    void setMaxChildrenMemory(usz bytes);
    usz totalChildrenMemory() const;

    // -- Memory --

    /** @brief Adds a memory translation entry. */
    void translate(const MemoryTranslation& mt);

    /**
     * @brief Copies bytes between addresses, respecting translation gaps.
     */
    void copy(usz source, usz dest, usz length);

    /**
     * @brief Allocates physical memory and maps it at the given virtual address.
     *
     * For child tasks: allocates within the parent's memory space. The parent
     * owns all child memory — this is the fundamental ownership model.
     * Created regions are writable but NOT executable (W^X policy).
     */
    void alloc(usz dest, usz length);



    /**
     * @brief Maps existing physical memory from `source` to `dest`.
     *
     * When a parent maps memory for a child, it maps from the caller's
     * (parent's) own memory, not from arbitrary/top memory.
     * A task cannot map memory for itself (Task::current() == self is blocked).
     */
    void map(usz source, usz dest, usz length);

    /** @brief Unmaps a virtual region. */
    void unmap(usz dest, usz length);

    /**
     * @brief Removes all memory maps, memory handlers, and activates
     *        memory isolation. The task sees its own address space from 0.
     *        After this call, the task must never escape its container.
     */
    void unmap();

    // -- IPC --

    /** @brief Sends a message to another task. Shares sender to receiver. */
    void send(Task& receiver, const String& payload);

    /**
     * @brief Shares a task object with this task (read-only access).
     */
    void share(Task& taskObj);

    /** @brief Direct access to the inbox. */
    Array<Message>& inbox() { return _state->inbox; }
    const Array<Message>& inbox() const { return _state->inbox; }

    /** @brief Direct access to the log. */
    Array<String>& log() { return _state->log; }
    const Array<String>& log() const { return _state->log; }

    // -- Scheduling --

    /**
     * @brief Sets the compute quota in microseconds per scheduling period.
     *
     * A quota of 0 means unlimited (background priority / as much as possible).
     */
    void setQuota(u64 us);

    /**
     * @brief Sets the minimum quota that each child task is forced to have.
     *
     * Fork bomb protection: when a parent spawns a child, the child gets
     * at least this much quota. If the parent's own quota is limited,
     * the child's quota is subtracted from the parent's remaining capacity.
     * If insufficient capacity, spawn fails.
     *
     * @param us  Minimum child quota in microseconds. 0 = no minimum.
     */
    void setMinChildQuota(u64 us);

    /**
     * @brief Overrides the default memory size for auto-allocation.
     *
     * Only takes effect if called before start() and before any manual
     * alloc() call. If alloc() has been called, this is ignored.
     *
     * @param bytes  Memory size in bytes (default: XI_DEFAULT_TASK_MEM = 4096).
     */
    void setMemorySize(usz bytes);

    // -- Callbacks --

    /**
     * @brief Sets the onFetch callback.
     *
     * Triggered when the task fetches memory that is not mapped or
     * not yet AOT'd. The callback runs on the same core as the task.
     *
     * @param cb  Callback receiving (dest address, requested length).
     */
    void setOnFetch(Func<void(usz, usz)> cb);
    void setOnFetch(usz start, usz end, Func<void(usz, usz)> cb);
    void uncache(usz start, usz end);
    void uncache();

    void copyAndMap(usz source, usz dest, usz length);
    usz translate(usz destAddr, usz length = 0);

    // -- Children --

    /** @brief Returns the number of child tasks. */
    usz childCount() const { return _state ? _state->childIds.size() : 0; }

    /** @brief Returns a handle to the i-th child. */
    Task child(usz index);

    // -- Static API (forwarding to current()) --
    template <typename Dummy = void>
    static void resume() { current().resume(); }

    static void resume(Task t) { t.resume(); }

    template <typename Dummy = void>
    static void stop() { current().stop(); }

    template <typename Dummy = void>
    static void stop(u64 us) { current().stop(us); }

    template <typename Dummy = void>
    static void destroy() { current().destroy(); }

    template <typename Dummy = void>
    static void translate(const MemoryTranslation& mt) { current().translate(mt); }
    template <typename Dummy = void>
    static void copy(usz source, usz dest, usz length) { current().copy(source, dest, length); }
    template <typename Dummy = void>
    static void alloc(usz dest, usz length) { current().alloc(dest, length); }

    template <typename Dummy = void>
    static void map(usz source, usz dest, usz length) { current().map(source, dest, length); }
    template <typename Dummy = void>
    static void unmap(usz dest, usz length) { current().unmap(dest, length); }
    template <typename Dummy = void>
    static void unmap() { current().unmap(); }
    template <typename Dummy = void>
    static void setMinChildQuota(u64 us) { current().setMinChildQuota(us); }
    template <typename Dummy = void>
    static void send(Task& receiver, const String& payload) { current().send(receiver, payload); }
    template <typename Dummy = void>
    static void share(Task& taskObj) { current().share(taskObj); }
    template <typename Dummy = void>
    static void setQuota(u64 us) { current().setQuota(us); }
    template <typename Dummy = void>
    static void setMemorySize(usz bytes) { current().setMemorySize(bytes); }
    template <typename Dummy = void>
    static void setOnFetch(Func<void(usz, usz)> cb) { current().setOnFetch(cb); }
    template <typename Dummy = void>
    static void setOnFetch(usz start, usz end, Func<void(usz, usz)> cb) { current().setOnFetch(start, end, cb); }
    template <typename Dummy = void>
    static void uncache(usz start, usz end) { current().uncache(start, end); }
    template <typename Dummy = void>
    static void uncache() { current().uncache(); }
    template <typename Dummy = void>
    static void copyAndMap(usz source, usz dest, usz length) { current().copyAndMap(source, dest, length); }
    template <typename Dummy = void>
    static usz translate(usz destAddr, usz length = 0) { return current().translate(destAddr, length); }
    template <typename Dummy = void>
    static void onInstruction(const String& instruction, Func<void()> callback) {
        current().onInstruction(instruction, callback);
    }
    template <typename Dummy = void>
    static void offInstruction(const String& instruction) {
        current().offInstruction(instruction);
    }
    template <typename Dummy = void>
    static void onInstructionTranslate(const String& instruction, Func<Array<u8>(const Array<u8>&)> callback) {
        current().onInstructionTranslate(instruction, callback);
    }
    template <typename Dummy = void>
    static void forwardInstruction(const String& instruction) {
        current().forwardInstruction(instruction);
    }
    template <typename Dummy = void>
    static void setOnSwap(Func<void(usz, usz)> cb) {
        current().setOnSwap(cb);
    }
    template <typename Dummy = void>
    static void setOnStore(Func<void(usz, usz)> cb) {
        current().setOnStore(cb);
    }
    template <typename Dummy = void>
    static void setOnStore(usz start, usz end, Func<void(usz, usz)> cb) {
        current().setOnStore(start, end, cb);
    }
    template <typename Dummy = void>
    static void setMaxChildrenMemory(usz bytes) {
        current().setMaxChildrenMemory(bytes);
    }
    template <typename Dummy = void>
    static usz totalChildrenMemory() {
        return current().totalChildrenMemory();
    }
    template <typename Dummy = void>
    static void setPin(usz coreId) { current().setPin(coreId); }
    template <typename Dummy = void>
    static void clearPin() { current().clearPin(); }

    template <typename Dummy = void>
    static Task spawn() { return current().spawn(); }






    template <typename Dummy = void>
    static usz childCount() { return current().childCount(); }

    template <typename Dummy = void>
    static Task child(usz index) { return current().child(index); }

    template <typename Dummy = void> static usz id() { return current().id(); }
    template <typename Dummy = void> static usz parentId() { return current().parentId(); }
    template <typename Dummy = void> static TaskStatus status() { return current().status(); }
    template <typename Dummy = void> static bool valid() { return current().valid(); }
    template <typename Dummy = void> static bool isPinned() { return current().isPinned(); }

private:
    template <typename Fn, typename... Args>
    void _execute_impl(int mode, usz targetId, Fn fn, Args... args);

    void _execute_impl_raw(int mode, usz targetId, void (*fn)(void*), void* arg);

    static void reset();
    friend void xi_reset_task_state_for_tests();
};



// -------------------------------------------------------------------------
// async() Implementation
// -------------------------------------------------------------------------

/**
 * @brief Internal wrapper used by Task::async to bridge C++ functions
 *        into the task entry point convention.
 */
struct TaskTrampoline {
    virtual ~TaskTrampoline() = default;
    virtual void run() = 0;
};

template <typename Lambda>
struct TaskTrampolineImpl : public TaskTrampoline {
    Lambda lambda;
    TaskTrampolineImpl(Lambda&& l) : lambda(Xi::Move(l)) {}
    void run() override {
        lambda();
    }
};

template <typename Fn, typename... Args>
struct AsyncWrapper {
    Fn fn;
    // Note: args are passed via inbox, not captured here.
    // This wrapper is stored as the task's entryArg.

    static void entry(void* self) {
        // The actual argument unmarshalling and function call happens
        // in Task.cpp via a type-erased trampoline.
        // This static entry is the task's native entry point.
        auto* wrapper = static_cast<AsyncWrapper*>(self);
        (void)wrapper;
        // Implementation in Task.cpp handles the inbox → args → fn → log flow.
    }
};

template <typename Fn, typename... Args>
Task Task::async(Fn fn, Args... args) {
    Task child = spawn();
    if (!child.valid()) return child;

    // Push args into the child's inbox as the first message.
    String argPayload;
    // Serialize each argument into the payload.
    // The async entry wrapper will deserialize them.
    (void)(argPayload += ... += Xi::serialize<decltype(args)>(args));
    child._state->inbox.push(Message{id(), Xi::Move(argPayload)});

    // Create wrapper and set entry.
    // We heap-allocate the wrapper since the task outlives this scope.
    using Wrapper = AsyncWrapper<Fn, Args...>;
    auto* wrapper = new Wrapper{fn};
    child._state->entryFn = &Wrapper::entry;
    child._state->entryArg = wrapper;

    return child;
}

template <typename Fn, typename... Args>
void Task::_execute_impl(int mode, usz targetId, Fn fn, Args... args) {
    if (!_state) return;

    Task caller = Task::current();
    bool insideTask = (xi_get_current_task() != nullptr);

    if (caller.valid()) {
        if (_state->id != caller.id() && _state->parentId != caller.id()) {
            return; // Blocked: not self and not child.
        }
    }

    if (_state->status == TaskStatus::Running && (!insideTask || _state->id != caller.id())) {
        _state->status = TaskStatus::Paused;
        while (_state->status == TaskStatus::Running) {
            if (insideTask) {
                Task::current().yield(caller._state->currentCore);
            }
        }
    }

    if (!_state->stack) {
        usz stackSz = getStackSize();
        _state->stack = new u8[stackSz];
        _state->stackSize = stackSz;
        _state->stackOwned = true;
    }

    auto lambda = [fn, args...]() {
        fn(args...);
    };

    using Impl = TaskTrampolineImpl<decltype(lambda)>;
    auto* trampoline = new Impl(Xi::Move(lambda));

    _state->entryFn = [](void* arg) {
        auto* tramp = static_cast<TaskTrampoline*>(arg);
        tramp->run();
        delete tramp;
    };
    _state->entryArg = trampoline;

    xi_context_init(&_state->context, _state->entryFn, _state->entryArg, _state->stack, _state->stackSize);

    if (mode == 1) {
        _state->isWaitingForMessage = true;
        _state->status = TaskStatus::Paused;
    } else if (mode == 2) {
        _state->waitDeadTarget = targetId;
        _state->status = TaskStatus::Paused;
    }

    if (insideTask && _state->id == caller.id()) {
        if (mode == 0) {
            if (!xi_validate_context_before_switch(_state)) {
                std::abort();
            }
            _state->status = TaskStatus::Running;
            TaskContext dummyContext;
            xi_context_switch(&dummyContext, &_state->context);
        } else {
            usz coreId = _state->currentCore;
            if (coreId < Task::_cores.size()) {
                CoreState& core = Task::_cores[coreId];
                TaskState* next = Task::pickNext(coreId);
                if (!next && Task::_tasks.size() > 0 && Task::_tasks[0]) {
                    next = Task::_tasks[0];
                }
                if (next) {
                    if (!xi_validate_context_before_switch(next)) {
                        next->status = TaskStatus::Destroyed;
                        core.currentTaskId = 0;
                        xi_set_current_task(nullptr);
                        TaskContext dummyContext;
                        xi_context_switch(&dummyContext, &core.idleContext);
                    } else {
                        next->status = TaskStatus::Running;
                        core.currentTaskId = next->id;
                        xi_set_current_task(next);
                        TaskContext dummyContext;
                        xi_context_switch(&dummyContext, &next->context);
                    }
                } else {
                    core.currentTaskId = 0;
                    xi_set_current_task(nullptr);
                    TaskContext dummyContext;
                    xi_context_switch(&dummyContext, &core.idleContext);
                }
            }
        }
    } else {
        if (mode == 0) {
            _state->status = TaskStatus::Ready;
            Task::enqueue(_state->id);
        }
    }

    if (!insideTask && mode == 2) {
        if (_state->id == 0) {
            while (true) {
                bool anyAlive = false;
                for (usz i = 1; i < Task::_tasks.size(); ++i) {
                    if (Task::_tasks[i] && Task::_tasks[i]->status != TaskStatus::Finished && Task::_tasks[i]->status != TaskStatus::Destroyed) {
                        anyAlive = true;
                        break;
                    }
                }
                if (!anyAlive) break;
                for (usz c = 0; c < Task::_cores.size(); ++c) {
                    if (Task::_cores[c].enabled) {
                        Task::yield(c);
                    }
                }
            }
        } else {
            while (!xi_is_task_dead_recursive(_state->id)) {
                for (usz c = 0; c < Task::_cores.size(); ++c) {
                    if (Task::_cores[c].enabled) {
                        Task::yield(c);
                    }
                }
            }
        }
    }
}

} // namespace Execution

#define XI_DECLARE_GLOBAL_EXECUTION_CONVENIENCE(name) \
    template <typename Fn, typename... Args> \
    void name(Fn fn, Args... args) { \
        Execution::Task::current().name(fn, args...); \
    } \
    inline void name(void (*fn)(void*), void* arg = nullptr) { \
        Execution::Task::current().name(fn, arg); \
    } \
    inline void name(usz addr) { \
        Execution::Task::current().name(addr); \
    } \
    inline void name() { \
        Execution::Task::current().name(); \
    }

namespace Execution {
    XI_DECLARE_GLOBAL_EXECUTION_CONVENIENCE(jump)
    XI_DECLARE_GLOBAL_EXECUTION_CONVENIENCE(wait)
    XI_DECLARE_GLOBAL_EXECUTION_CONVENIENCE(waitDead)

    inline void yield(usz coreId = 0) {
        Execution::Task::yield(coreId);
    }
}

/**
 * @brief Spawns a task under the currently executing task (or root if none),
 *        initializing it to execute fn(args...). Auto-starts.
 */
template <typename Fn, typename... Args>
Execution::Task spawn(Fn fn, Args... args) {
    Execution::Task parent = Execution::Task::current();
    if (!parent.valid()) {
        parent = Execution::Task::root();
    }
    return parent.spawn(fn, args...);
}

#endif // XI_EXECUTION_TASK_HPP
