/**
 * @file Task.hpp
 * @brief Task abstraction — the fundamental unit of execution in the Task subsystem.
 *
 * A Task unifies processes, threads, and functions under a single concept.
 * Tasks form a parent-child tree. Each task has its own pipes (IPC),
 * memory map, and scheduling quota.
 *
 * Convention:
 *   - Pipes replace inbox/log. A pipe holds bytes and sender task ID.
 *   - A pipe is owned by a task; the owner can close it, and if the
 *     owner is destroyed all its pipes are destroyed.
 *   - Bytes sent are in the sender's memory; parent/same-task reads
 *     use translation + CoW, cross-task reads use CoW.
 *   - The return value and args are data (not pipes) but CoW applies.
 *   - .copy is lazy (Copy-on-Write). .physicalCopy is immediate.
 *   - A sender is automatically shared to the receiver on message send.
 *   - A task can push to parent, children, and shared tasks.
 */

#ifndef XI_EXECUTION_TASK_HPP
#define XI_EXECUTION_TASK_HPP

#include "../Collection/Array.hpp"
#include "../Collection/String.hpp"
#include "../Xi/Func.hpp"
#include "AOT.hpp"
#include "Context.hpp"
#include <cstdlib>
#include <type_traits>
#include <cstring>
#include <tuple>

/// Default memory allocation for a task if alloc() is not called before start().
static constexpr usz XI_DEFAULT_TASK_MEM = 4096;

namespace Task {

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

extern usz xi_last_guest_rbx;
extern usz xi_last_jit_rip;

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

extern GuestRegs* xi_guest_regs;
extern usz tl_currently_rewriting_physical;


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
    bool cow = false;   ///< If true, Copy-on-Write is active.
    u64 lastAccessTicks = 0; ///< Least Recently Used counter.
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
 * @brief An IPC message from one task to another (zero-copy pointer).
 * @note Legacy struct kept for backward compatibility with inbox.
 */
struct Message {
    usz senderId;       ///< ID of the sending task.
    void* payload;      ///< Pointer to message payload.
    usz size;           ///< Size of the payload structure.

    template <typename T>
    T* as() const {
        return static_cast<T*>(payload);
    }
};

/**
 * @struct LogEntry
 * @brief Logged pointer entry.
 * @note Legacy struct kept for backward compatibility with log.
 */
struct LogEntry {
    void* ptr;          ///< Pointer to logged structure.
    usz size;           ///< Size of the logged structure.

    template <typename T>
    T* as() const {
        return static_cast<T*>(ptr);
    }
};

/**
 * @struct PipeEntry
 * @brief A single chunk of bytes written into a Pipe.
 */
struct PipeEntry {
    usz senderId;       ///< ID of the sending task (0 = anonymous).
    u8* data;           ///< Pointer to the byte data (in sender's memory or CoW).
    usz size;           ///< Size in bytes.
    usz position;       ///< Byte offset of this entry within the pipe's stream.
    bool cow;           ///< If true, this entry is a CoW reference (not owned).
    bool isDummy;       ///< If true, this is a dummy/empty entry (no content).

    PipeEntry() : senderId(0), data(nullptr), size(0), position(0), cow(false), isDummy(false) {}

    template <typename T>
    T* as() const {
        return reinterpret_cast<T*>(data);
    }
};

/**
 * @struct Pipe
 * @brief A byte-oriented IPC channel owned by a task.
 *
 * A pipe holds PipeEntries. The owner task can close the pipe.
 * If the owner is destroyed, all owned pipes are destroyed.
 * Pipes serve as file descriptors in POSIX syscall emulation.
 *
 * Supports: read, write, pread, pwrite, dup, poll, seek, fcntl.
 * Default mode is blocking.
 */
struct Pipe {
    usz id;             ///< Unique pipe ID (acts as file descriptor).
    usz ownerId;        ///< Task ID that owns this pipe.
    Array<PipeEntry> entries; ///< Buffered entries.
    bool closed;        ///< If true, pipe is closed (no more writes).
    usz position;       ///< Current read cursor position (byte offset in stream).
    usz writePosition;  ///< Total bytes written (next write offset).
    bool blocking;      ///< If true, reads block when no data. Default: true.
    int flags;          ///< fcntl-style flags (O_NONBLOCK, etc.).
    Pipe* source;       ///< Source pipe if this is a duplicate.

    Func<void(Pipe*)> onWrite; ///< Callback invoked when data is written.
    Func<void(usz, usz, usz)> onRead; ///< Callback invoked on cache misses (uncached reads).
    usz logicalSize;    ///< Logical size of the file (to distinguish cache miss vs EOF).
    usz maxCachedBytes; ///< Maximum cached clean bytes (0 = no limit).

    Pipe() : id(0), ownerId(0), closed(false), position(0), writePosition(0), blocking(true), flags(0), source(nullptr), logicalSize(0), maxCachedBytes(0) {}

    void setBlocking(bool b) {
        blocking = b;
        if (!b) flags |= 0x800; // O_NONBLOCK
        else flags &= ~0x800;
    }

    void dummyWrite(usz pos, usz length);
    void setCaching(usz bytes);
    void flush();
    void flush(usz pos, usz length);
    void setLogicalSize(usz size) { logicalSize = size; }
    void checkEviction();
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

    // IPC - Legacy (kept for backward compatibility)
    Array<Message> inbox;
    Array<LogEntry> log;

    // IPC - Pipes
    Array<Pipe*> pipes;     ///< Pipes visible to this task (owned or subscribed).
    Array<usz> ownedPipeIds; ///< IDs of pipes this task owns.
    static Array<Pipe*> _allPipes;  ///< Global pipe registry.
    static usz _nextPipeId;

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
    Array<FetchRange> originalFetchRanges;

    struct CopyMapRegion {
        usz dest;
        usz size;
    };
    Array<CopyMapRegion> copyMapCache;

    // Entry point for raw tasks
    void (*entryFn)(void*);
    void* entryArg;
    bool isWaitingForMessage;
    bool isMemoryIsolated;        ///< If true, task is memory-isolated.
    bool isInstructionIsolated;   ///< If true, task is instruction-isolated.
    usz waitDeadTarget;     ///< Target task ID to wait for death (0 = none, -1 = all, -2 = children).
    usz brkAddr = 0;
    usz mmapAddr = 0;

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
    u64 accessCounter = 0;

    struct StoreRange {
        usz start;
        usz end;
        Func<void(usz, usz)> callback;
    };
    Array<StoreRange> storeRanges;

    struct SwapRange {
        usz start;
        usz end;
        Func<void(usz, usz)> callback;
    };
    Array<SwapRange> swapRanges;

    void* returnValue;
    usz returnValueSize;
    bool returnValueCow = false;   ///< If true, return value is a CoW reference.
    usz tid;
    usz fsBase;

    // CoW tracking for args (inbox[0] in legacy, or pipe-based)
    void* argsData = nullptr;
    usz argsSize = 0;
    bool argsCow = false;

    TaskState()
        : id(0), parentId(0), status(TaskStatus::Created),
          context{}, quotaUs(0), remainingUs(0), sleepUntilUs(0),
          pinnedCore(0), isPinned(false), currentCore(0),
          stack(nullptr), stackSize(0), stackOwned(false),
          isWaitingForMessage(false), isMemoryIsolated(false),
          isInstructionIsolated(false), waitDeadTarget(0),
          minChildQuotaUs(0), childQuotaUsed(0),
          maxChildrenMemory(0), accessCounter(0),
          returnValue(nullptr), returnValueSize(0),
          returnValueCow(false), tid(0), fsBase(0),
          argsData(nullptr), argsSize(0), argsCow(false) {}

    ~TaskState() {
        if (returnValue && !returnValueCow) {
            delete[] static_cast<u8*>(returnValue);
        }
        if (argsData && !argsCow) {
            delete[] static_cast<u8*>(argsData);
        }
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
    static usz _nextTid;
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
    static usz getAllocationRefCount(u8* ptr);

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
        usz size = maxReg > 0 ? maxReg : 1024 * 1024;
        if (size < 1024 * 1024) size = 1024 * 1024;
        return size;
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

    /** @brief Hook an instruction execution (single instruction name). */
    void onInstruction(const String& instruction, Func<void()> callback);

    /**
     * @brief Hook multiple sub-instructions under one call.
     * E.g., onInstruction({"syscall:0", "syscall:1", "syscall:9"}, cb)
     * registers the callback for each sub-instruction.
     */
    void onInstruction(const Array<String>& instructions, Func<void()> callback);

    /** @brief Ban an instruction and remove callback. */
    void offInstruction(const String& instruction);

    /** @brief Trap an instruction (replace with ud2). */
    void trapInstruction(const String& instruction);

    void onInstructionTranslate(const String& instruction, Func<Array<u8>(const Array<u8>&)> callback);

    /**
     * @brief Immediate physical memory copy (no CoW, always copies bytes).
     * Renamed from immediateCopy for clarity.
     */
    void physicalCopy(usz source, usz dest, usz length);

    /** @brief Legacy alias for physicalCopy. */
    void immediateCopy(usz source, usz dest, usz length) { physicalCopy(source, dest, length); }

    void onSwap(Func<void(usz, usz)> cb);
    void onSwap(usz start, usz end, Func<void(usz, usz)> cb);
    void onStore(Func<void(usz, usz)> cb);
    void onStore(usz start, usz end, Func<void(usz, usz)> cb);
    void setMaxChildrenMemory(usz bytes);
    usz totalChildrenMemory() const;

    // -- Pipes --

    /**
     * @brief Opens a new pipe owned by this task.
     * @return The pipe ID (also serves as a file descriptor).
     */
    usz openPipe();

    /**
     * @brief Closes and frees a pipe by ID. Only the owner can close.
     */
    void closePipe(usz pipeId);

    /** @brief Legacy alias. */
    void freePipe(usz pipeId) { closePipe(pipeId); }

    /**
     * @brief Returns a pointer to a pipe by ID, or nullptr.
     */
    Pipe* getPipe(usz pipeId);

    /**
     * @brief Returns the array of pipes visible to this task.
     */
    const Array<Pipe*>& pipeList() const { return _state->pipes; }

    // --- Pipe I/O ---

    /**
     * @brief Write bytes to a pipe. Advances pipe's writePosition.
     * @return Number of bytes written.
     */
    usz write(Pipe* pipe, const void* data, usz length);

    /**
     * @brief Read bytes from a pipe. Advances pipe's position.
     * If blocking and no data, returns 0 (task should yield/retry).
     * If non-blocking and no data, returns 0.
     * @return Number of bytes read.
     */
    usz read(Pipe* pipe, void* buf, usz length);

    /**
     * @brief Read at a specific offset without changing the pipe's position.
     * @return Number of bytes read.
     */
    usz pread(Pipe* pipe, void* buf, usz length, usz offset);

    /**
     * @brief Write at a specific offset without changing writePosition.
     * @return Number of bytes written.
     */
    usz pwrite(Pipe* pipe, const void* data, usz length, usz offset);

    /**
     * @brief Duplicate a pipe (like dup3). Returns new pipe ID.
     * The new pipe shares the same entries array (CoW reference).
     * @param pipe  The pipe to duplicate.
     * @param flags dup3-style flags (0 = default).
     * @return New pipe ID.
     */
    usz dup(Pipe* pipe, int flags = 0);

    /** @brief dup by fd number (POSIX dup). */
    usz dup(usz fd) {
        Pipe* p = getPipe(fd);
        return p ? dup(p, 0) : 0;
    }

    /** @brief dup2 — duplicate to a specific fd (closes old fd first). */
    usz dup2(usz oldfd, usz newfd);

    /** @brief dup3 — duplicate with flags. */
    usz dup3(usz oldfd, usz newfd, int flags);

    /**
     * @brief Check if pipe has data available to read.
     * @return true if data is available at current position.
     */
    bool poll(Pipe* pipe);

    /**
     * @brief Poll and copy the next entry's data into outBuf.
     * @return Pointer to the PipeEntry read, or nullptr if no data.
     */
    PipeEntry* poll(Pipe* pipe, void* outBuf);

    /**
     * @brief Poll with callback. If data is available, calls fn().
     * @return true if callback was invoked.
     */
    bool poll(Pipe* pipe, Func<void()> fn);

    /**
     * @brief Seek to a byte position in the pipe's stream.
     */
    void seek(Pipe* pipe, usz pos);

    /**
     * @brief fcntl-style operations on a pipe.
     * Supports F_GETFL (3), F_SETFL (4), F_GETFD (1), F_SETFD (2).
     * @return Result value, or -1 on error.
     */
    long fcntl(Pipe* pipe, int cmd, long arg = 0);

    // -- Memory --

    /** @brief Checks if a virtual memory range overlaps with any mapped regions. */
    bool isMapped(usz base, usz size) const;

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

    // -- IPC (Legacy + Pipes) --

    // Legacy IPC methods (kept for backward compatibility, now also push to pipes)

    /** @brief Log a raw pointer payload. */
    void log(void* ptr, usz size);

    /** @brief Log a string. */
    void log(const char* str) {
        usz len = 0;
        while (str[len]) {
            len++;
        }
        log(const_cast<char*>(str), len + 1);
    }

    /** @brief Log a typed object pointer. */
    template <typename T>
    void log(T* obj) {
        log(static_cast<void*>(obj), sizeof(T));
    }

    /** @brief Shares a task object with this task (read-only access). */
    void share(Task& taskObj);

    /** @brief Direct access to the inbox (legacy). */
    Array<Message>& inbox() { return _state->inbox; }
    const Array<Message>& inbox() const { return _state->inbox; }

    /** @brief Direct access to the log (legacy). */
    Array<LogEntry>& log() { return _state->log; }
    const Array<LogEntry>& log() const { return _state->log; }

    /** @brief Get the task ID (.tid) */
    usz tid() const { return _state ? _state->tid : 0; }

    /** @brief Check if the task has a return value. */
    bool hasReturnValue() const {
        return _state && _state->returnValue != nullptr;
    }

    /** @brief Get the raw return value by copying bytes. */
    template <typename T>
    T returnValue() const {
        static_assert(std::is_trivially_copyable_v<T>,
                      "Return type must be trivially copyable.");
        if (!_state || !_state->returnValue || _state->returnValueSize != sizeof(T)) {
            return T{};
        }
        T res;
        std::memcpy(&res, _state->returnValue, sizeof(T));
        return res;
    }

    /** @brief Lazy-copy / translate a log entry for a specific reader task. */
    LogEntry getLogEntry(usz index, Task reader = Task::current()) const;

    /** @brief Lazy-copy / translate the return value for a specific reader task. */
    void* getReturnValue(Task reader = Task::current()) const;

    /** @brief Type-safe helper to fetch a lazily-copied typed log entry. */
    template <typename T>
    T* logEntryAs(usz index, Task reader = Task::current()) const {
        return getLogEntry(index, reader).as<T>();
    }

    /** @brief Type-safe helper to fetch a pointer to a lazily-copied return value. */
    template <typename T>
    T* returnValuePtr(Task reader = Task::current()) const {
        return static_cast<T*>(getReturnValue(reader));
    }


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
    void onFetch(Func<void(usz, usz)> cb);
    void onFetch(usz start, usz end, Func<void(usz, usz)> cb);
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
    static void physicalCopy(usz source, usz dest, usz length) { current().physicalCopy(source, dest, length); }
    template <typename Dummy = void>
    static void immediateCopy(usz source, usz dest, usz length) { current().physicalCopy(source, dest, length); }
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
    static usz openPipe() { return current().openPipe(); }
    template <typename Dummy = void>
    static void closePipe(usz pipeId) { current().closePipe(pipeId); }
    template <typename Dummy = void>
    static void freePipe(usz pipeId) { current().closePipe(pipeId); }
    template <typename Dummy = void>
    static Pipe* getPipe(usz pipeId) { return current().getPipe(pipeId); }

    // Pipe I/O static forwarders (pipe as first arg)
    template <typename Dummy = void>
    static usz write(Pipe* pipe, const void* data, usz length) { return current().write(pipe, data, length); }
    template <typename Dummy = void>
    static usz read(Pipe* pipe, void* buf, usz length) { return current().read(pipe, buf, length); }
    template <typename Dummy = void>
    static usz pread(Pipe* pipe, void* buf, usz length, usz offset) { return current().pread(pipe, buf, length, offset); }
    template <typename Dummy = void>
    static usz pwrite(Pipe* pipe, const void* data, usz length, usz offset) { return current().pwrite(pipe, data, length, offset); }
    template <typename Dummy = void>
    static usz dup(Pipe* pipe, int flags = 0) { return current().dup(pipe, flags); }
    template <typename Dummy = void>
    static usz dup(usz fd) { return current().dup(fd); }
    template <typename Dummy = void>
    static usz dup2(usz oldfd, usz newfd) { return current().dup2(oldfd, newfd); }
    template <typename Dummy = void>
    static usz dup3(usz oldfd, usz newfd, int flags) { return current().dup3(oldfd, newfd, flags); }
    template <typename Dummy = void>
    static bool poll(Pipe* pipe) { return current().poll(pipe); }
    template <typename Dummy = void>
    static PipeEntry* poll(Pipe* pipe, void* outBuf) { return current().poll(pipe, outBuf); }
    template <typename Dummy = void>
    static bool poll(Pipe* pipe, Func<void()> fn) { return current().poll(pipe, fn); }
    template <typename Dummy = void>
    static void seek(Pipe* pipe, usz pos) { current().seek(pipe, pos); }
    template <typename Dummy = void>
    static long fcntl(Pipe* pipe, int cmd, long arg = 0) { return current().fcntl(pipe, cmd, arg); }

    template <typename Dummy = void>
    static void log(void* ptr, usz size) { current().log(ptr, size); }
    template <typename Dummy = void>
    static void log(const char* str) { current().log(str); }

    template <typename Dummy = void>
    static Task parent() { return current().parent(); }
    template <typename Dummy = void>
    static usz tid() { return current().tid(); }
    template <typename Dummy = void>
    static Array<Message>& inbox() { return current().inbox(); }
    template <typename Dummy = void>
    static Array<LogEntry>& log() { return current().log(); }
    template <typename Dummy = void>
    static void share(Task& taskObj) { current().share(taskObj); }
    template <typename Dummy = void>
    static void setQuota(u64 us) { current().setQuota(us); }
    template <typename Dummy = void>
    static void setMemorySize(usz bytes) { current().setMemorySize(bytes); }
    template <typename Dummy = void>
    static void onFetch(Func<void(usz, usz)> cb) { current().onFetch(cb); }
    template <typename Dummy = void>
    static void onFetch(usz start, usz end, Func<void(usz, usz)> cb) { current().onFetch(start, end, cb); }
    template <typename Dummy = void>
    static void uncache(usz start, usz end) { current().uncache(start, end); }
    template <typename Dummy = void>
    static void uncache() { current().uncache(); }
    template <typename Dummy = void>
    static bool isMapped(usz base, usz size) { return current().isMapped(base, size); }
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
    static void trapInstruction(const String& instruction) {
        current().trapInstruction(instruction);
    }
    template <typename Dummy = void>
    static void onInstructionTranslate(const String& instruction, Func<Array<u8>(const Array<u8>&)> callback) {
        current().onInstructionTranslate(instruction, callback);
    }
    template <typename Dummy = void>
    static void onSwap(Func<void(usz, usz)> cb) {
        current().onSwap(cb);
    }
    template <typename Dummy = void>
    static void onSwap(usz start, usz end, Func<void(usz, usz)> cb) {
        current().onSwap(start, end, cb);
    }
    template <typename Dummy = void>
    static void onStore(Func<void(usz, usz)> cb) {
        current().onStore(cb);
    }
    template <typename Dummy = void>
    static void onStore(usz start, usz end, Func<void(usz, usz)> cb) {
        current().onStore(start, end, cb);
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

    static void entry(void* self) {
        auto* wrapper = static_cast<AsyncWrapper*>(self);
        TaskState* state = xi_get_current_task();
        if (!state || state->inbox.size() == 0) return;

        // 1. Get the payload pointer and size from inbox[0]
        const Message& msg = state->inbox[0];
        
        // 2. Deserialize arguments from the payload
        String payload(static_cast<const u8*>(msg.payload), msg.size);
        usz at = 0;
        std::tuple<Args...> argsTuple = { Xi::deserialize<Args>(payload, at)... };

        // 3. Clean up the heap-allocated payload buffer since we took ownership
        delete[] static_cast<u8*>(msg.payload);

        // 4. Call the function and capture return value
        if constexpr (std::is_void_v<std::invoke_result_t<Fn, Args...>>) {
            std::apply(wrapper->fn, argsTuple);
        } else {
            auto res = std::apply(wrapper->fn, argsTuple);
            using ReturnType = decltype(res);
            static_assert(std::is_trivially_copyable_v<ReturnType>, 
                          "Return type must be trivially copyable for raw byte copy.");
            if (state->returnValue) {
                delete[] static_cast<u8*>(state->returnValue);
            }
            state->returnValueSize = sizeof(ReturnType);
            state->returnValue = new u8[sizeof(ReturnType)];
            std::memcpy(state->returnValue, &res, sizeof(ReturnType));
        }
    }
};

template <typename Fn, typename... Args>
Task Task::async(Fn fn, Args... args) {
    Task child = spawn();
    if (!child.valid()) return child;

    // Push args into the child's inbox as the first message.
    String argPayload;
    (void)(argPayload += ... += Xi::serialize<decltype(args)>(args));
    
    // Allocate a heap buffer for the serialized arguments
    u8* buffer = new u8[argPayload.size()];
    std::memcpy(buffer, argPayload.data(), argPayload.size());

    Message msg;
    msg.senderId = id();
    msg.payload = buffer;
    msg.size = argPayload.size();
    child._state->inbox.push(msg);

    // Create wrapper and set entry.
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

    auto lambda = [fn, args..., state = _state]() {
        if constexpr (std::is_void_v<std::invoke_result_t<Fn, Args...>>) {
            fn(args...);
        } else {
            auto res = fn(args...);
            using ReturnType = decltype(res);
            static_assert(std::is_trivially_copyable_v<ReturnType>,
                          "Return type must be trivially copyable for raw byte copy.");

            if (state->returnValue) {
                delete[] static_cast<u8*>(state->returnValue);
            }

            state->returnValueSize = sizeof(ReturnType);
            state->returnValue = new u8[sizeof(ReturnType)];
            std::memcpy(state->returnValue, &res, sizeof(ReturnType));
        }
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

    // Pipe namespace-level convenience functions
    inline usz openPipe() {
        return Task::current().openPipe();
    }

    inline void closePipe(usz pipeId) {
        Task::current().closePipe(pipeId);
    }

    inline void freePipe(usz pipeId) {
        Task::current().closePipe(pipeId);
    }

    inline Pipe* getPipe(usz pipeId) {
        return Task::current().getPipe(pipeId);
    }

    inline usz write(Pipe* pipe, const void* data, usz length) {
        return Task::current().write(pipe, data, length);
    }

    inline usz read(Pipe* pipe, void* buf, usz length) {
        return Task::current().read(pipe, buf, length);
    }

    inline usz pread(Pipe* pipe, void* buf, usz length, usz offset) {
        return Task::current().pread(pipe, buf, length, offset);
    }

    inline usz pwrite(Pipe* pipe, const void* data, usz length, usz offset) {
        return Task::current().pwrite(pipe, data, length, offset);
    }

    inline usz dup(Pipe* pipe, int flags = 0) {
        return Task::current().dup(pipe, flags);
    }

    inline bool poll(Pipe* pipe) {
        return Task::current().poll(pipe);
    }

    inline PipeEntry* poll(Pipe* pipe, void* outBuf) {
        return Task::current().poll(pipe, outBuf);
    }

    inline bool poll(Pipe* pipe, Func<void()> fn) {
        return Task::current().poll(pipe, fn);
    }

    inline void seek(Pipe* pipe, usz pos) {
        Task::current().seek(pipe, pos);
    }

    inline long fcntl(Pipe* pipe, int cmd, long arg = 0) {
        return Task::current().fcntl(pipe, cmd, arg);
    }

} // namespace Task

#define XI_DECLARE_GLOBAL_EXECUTION_CONVENIENCE(name) \
    template <typename Fn, typename... Args> \
    void name(Fn fn, Args... args) { \
        Task::Task::current().name(fn, args...); \
    } \
    inline void name(void (*fn)(void*), void* arg = nullptr) { \
        Task::Task::current().name(fn, arg); \
    } \
    inline void name(usz addr) { \
        Task::Task::current().name(addr); \
    } \
    inline void name() { \
        Task::Task::current().name(); \
    }

namespace Task {
    XI_DECLARE_GLOBAL_EXECUTION_CONVENIENCE(jump)
    XI_DECLARE_GLOBAL_EXECUTION_CONVENIENCE(wait)
    XI_DECLARE_GLOBAL_EXECUTION_CONVENIENCE(waitDead)

    inline void yield(usz coreId = 0) {
        Task::Task::yield(coreId);
    }



    template <typename T>
    inline void log(T* obj) {
        Task::current().log(obj);
    }

    inline void log(void* ptr, usz size) {
        Task::current().log(ptr, size);
    }

    inline void log(const char* str) {
        Task::current().log(str);
    }

    inline Task parent() {
        return Task::current().parent();
    }

    inline usz tid() {
        return Task::current().tid();
    }

    inline usz id() {
        return Task::current().id();
    }

    inline usz parentId() {
        return Task::current().parentId();
    }

    inline Array<Message>& inbox() {
        return Task::current().inbox();
    }

    inline Array<LogEntry>& log() {
        return Task::current().log();
    }

    inline TaskStatus status() {
        return Task::current().status();
    }

    inline bool valid() {
        return Task::current().valid();
    }

    inline bool isPinned() {
        return Task::current().isPinned();
    }
}

/**
 * @brief Spawns a task under the currently executing task (or root if none),
 *        initializing it to execute fn(args...). Auto-starts.
 */
template <typename Fn, typename... Args>
Task::Task spawn(Fn fn, Args... args) {
    Task::Task parent = Task::Task::current();
    if (!parent.valid()) {
        parent = Task::Task::root();
    }
    return parent.spawn(fn, args...);
}

#endif // XI_EXECUTION_TASK_HPP
