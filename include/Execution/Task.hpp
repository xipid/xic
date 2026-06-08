/**
 * @file Task.hpp
 * @brief Task abstraction — the fundamental unit of execution in Tasker.
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

/// Default memory allocation for a task if alloc() is not called before start().
static constexpr usz XI_DEFAULT_TASK_MEM = 4096;

namespace Execution {

using namespace Xi;
using namespace Collection;

class Tasker; // Forward declaration.

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
 * @brief Internal state managed by the Tasker. Not exposed to user code.
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
    Func<void(usz dest, usz length)> onFetch;

    // Entry point for raw tasks
    void (*entryFn)(void*);
    void* entryArg;

    TaskState()
        : id(0), parentId(0), status(TaskStatus::Created),
          context{}, quotaUs(0), remainingUs(0), sleepUntilUs(0),
          pinnedCore(0), isPinned(false), currentCore(0),
          stack(nullptr), stackSize(0), stackOwned(false),
          entryFn(nullptr), entryArg(nullptr) {}
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
 * Tasks are always created through Tasker or via an existing Task's
 * spawn()/async() methods.
 */
class XI_EXPORT Task {
public:
    TaskState* _state;  ///< Internal state (public for Tasker access).
    Tasker* _tasker;    ///< Owning Tasker instance.

    Task() : _state(nullptr), _tasker(nullptr) {}
    Task(TaskState* state, Tasker* tasker) : _state(state), _tasker(tasker) {}
    ~Task() = default;

    // -- Identity --

    /** @brief Returns the task ID. */
    usz id() const { return _state ? _state->id : 0; }

    /** @brief Returns the parent task ID. */
    usz parentId() const { return _state ? _state->parentId : 0; }

    /** @brief Returns the current status. */
    TaskStatus status() const {
        return _state ? _state->status : TaskStatus::Destroyed;
    }

    /** @brief Returns true if the task handle is valid. */
    bool valid() const { return _state != nullptr && _tasker != nullptr; }

    /** @brief Returns a handle to the currently executing task on this core. */
    static Task current();

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
     * @brief Spawns a child task wrapping a C++ function.
     *
     * The wrapper:
     *   1. On first start, shifts args from inbox into the C++ parameter list.
     *   2. Calls the function.
     *   3. Pushes the return value into log.
     */
    template <typename Fn, typename... Args>
    Task async(Fn fn, Args... args);

    /**
     * @brief Sets the entry point for a raw (non-async) task.
     *
     * The entry function receives a void* argument and runs until return.
     */
    void setEntry(void (*fn)(void*), void* arg);

    /**
     * @brief Starts or resumes execution.
     *
     * If no memory has been allocated via alloc(), automatically allocates
     * XI_DEFAULT_TASK_MEM (4KB) at virtual address 0 for the task to use
     * as stack, heap, or however it sees fit.
     */
    void start();

    /** @brief Pauses execution indefinitely (cooperative yield). */
    void stop();

    /** @brief Pauses execution for the given number of microseconds. */
    void stop(u64 us);

    /** @brief Sets the program counter to a new address. Triggers AOT. */
    void jump(usz addr);

    /** @brief Destroys this task and all its children. Frees all memory. */
    void destroy();

    // -- Memory --

    /** @brief Adds a memory translation entry. */
    void translate(const MemoryTranslation& mt);

    /**
     * @brief Copies bytes between addresses, respecting translation gaps.
     */
    void copy(usz source, usz dest, usz length);

    /**
     * @brief Allocates physical memory and maps it at the given virtual address.
     */
    void alloc(usz dest, usz length);

    /**
     * @brief Maps existing physical memory from `source` to `dest`.
     */
    void mmap(usz source, usz dest, usz length);

    /** @brief Unmaps a virtual region. */
    void unmap(usz dest, usz length);

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

    // -- Children --

    /** @brief Returns the number of child tasks. */
    usz childCount() const { return _state ? _state->childIds.size() : 0; }

    /** @brief Returns a handle to the i-th child. */
    Task child(usz index);
};

// -------------------------------------------------------------------------
// async() Implementation
// -------------------------------------------------------------------------

/**
 * @brief Internal wrapper used by Task::async to bridge C++ functions
 *        into the task entry point convention.
 */
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

} // namespace Execution

#endif // XI_EXECUTION_TASK_HPP
