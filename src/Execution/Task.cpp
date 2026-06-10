/**
 * @file Task.cpp
 * @brief Task lifecycle, IPC, and memory operations — portable C++.
 */

#include "../../include/Execution/Task.hpp"
#include "../../include/Execution/Tasker.hpp"

// Portable microsecond timestamp.
#if defined(__x86_64__) || defined(_M_X64) || defined(__linux__)
#include <ctime>
static Xi::u64 xi_micros_now() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (Xi::u64)ts.tv_sec * 1000000ULL + (Xi::u64)(ts.tv_nsec / 1000);
}
#elif defined(ESP_PLATFORM)
#include <esp_timer.h>
static Xi::u64 xi_micros_now() {
    return (Xi::u64)esp_timer_get_time();
}
#else
static Xi::u64 xi_micros_now() { return 0; }
#endif

namespace Execution {

// -------------------------------------------------------------------------
// Static: current task
// -------------------------------------------------------------------------

static thread_local TaskState* tl_currentTask = nullptr;

Task Task::current() {
    Task t;
    if (tl_currentTask && Tasker::instance) {
        t._state = tl_currentTask;
        t._tasker = Tasker::instance;
    }
    return t;
}

// Used by the context entry trampoline to set the thread-local current task.
void xi_set_current_task(TaskState* s) {
    tl_currentTask = s;
}

TaskState* xi_get_current_task() {
    return tl_currentTask;
}

// -------------------------------------------------------------------------
// Core Affinity
// -------------------------------------------------------------------------

void Task::setPin(usz coreId) {
    if (!_state) return;
    _state->isPinned = true;
    _state->pinnedCore = coreId;
}

void Task::clearPin() {
    if (!_state) return;
    _state->isPinned = false;
}

// -------------------------------------------------------------------------
// Lifecycle
// -------------------------------------------------------------------------

Task Task::spawn() {
    Task child;
    if (!_state || !_tasker) return child;

    TaskState* cs = _tasker->allocTask(_state->id);
    if (!cs) return child;

    child._state = cs;
    child._tasker = _tasker;

    _state->childIds.push(cs->id);

    // Parent is always implicitly shared to child.
    cs->sharedIds.push(_state->id);

    return child;
}

void Task::setEntry(void (*fn)(void*), void* arg) {
    if (!_state) return;
    _state->entryFn = fn;
    _state->entryArg = arg;
}

void Task::start() {
    if (!_state || !_tasker) return;

    // Auto-allocate memory if none has been allocated.
    if (_state->regions.size() == 0) {
        usz memSize = _state->stackSize > 0 ? _state->stackSize : XI_DEFAULT_TASK_MEM;
        alloc(0, memSize);
    }

    // Allocate a context stack if not yet done.
    if (!_state->stack) {
        // Use the task's first memory region as the stack area,
        // or allocate a separate stack for the context switch mechanism.
        usz stackSz = _state->stackSize > 0 ? _state->stackSize : XI_DEFAULT_TASK_MEM;
        _state->stack = new u8[stackSz];
        _state->stackSize = stackSz;
        _state->stackOwned = true;
    }

    if (_state->status == TaskStatus::Created) {
        // First start: initialize context.
        if (_state->entryFn) {
            xi_context_init(&_state->context, _state->entryFn, _state->entryArg,
                            _state->stack, _state->stackSize);
        }
        _state->status = TaskStatus::Ready;
        _tasker->enqueue(_state->id);
    } else if (_state->status == TaskStatus::Paused ||
               _state->status == TaskStatus::Sleeping) {
        // Resume.
        _state->status = TaskStatus::Ready;
        _tasker->enqueue(_state->id);
    }
}

void Task::stop() {
    if (!_state || !_tasker) return;
    _state->status = TaskStatus::Paused;
    // If this is the currently running task, yield.
    usz core = _state->currentCore;
    if (_tasker->currentTask(core) == _state) {
        _tasker->yield(core);
    }
}

void Task::stop(u64 us) {
    if (!_state || !_tasker) return;
    _state->status = TaskStatus::Sleeping;
    _state->sleepUntilUs = xi_micros_now() + us;
    usz core = _state->currentCore;
    if (_tasker->currentTask(core) == _state) {
        _tasker->yield(core);
    }
}

void Task::jump(usz addr) {
    if (!_state) return;
#if defined(__x86_64__) || defined(_M_X64)
    _state->context.rip = (u64)addr;
#else
    _state->context.pc = (decltype(_state->context.pc))addr;
#endif
    // Invalidate AOT cache for this region — force re-AOT on next execution.
    AOT::invalidate(_state->aotCache, addr, 0);
}

void Task::destroy() {
    if (!_state || !_tasker) return;
    _tasker->destroyTask(_state->id);
    _state = nullptr;
}

// -------------------------------------------------------------------------
// Memory
// -------------------------------------------------------------------------

void Task::translate(const MemoryTranslation& mt) {
    if (!_state) return;
    _state->translations.push(mt);
}

void Task::copy(usz source, usz dest, usz length) {
    if (!_state || length == 0) return;

    // Find source physical address.
    u8* srcPhys = nullptr;
    usz srcOffset = 0;
    for (usz i = 0; i < _state->regions.size(); ++i) {
        MemoryRegion& r = _state->regions[i];
        if (source >= r.base && source < r.base + r.size) {
            srcPhys = r.physical;
            srcOffset = source - r.base;
            break;
        }
    }

    // Find dest physical address.
    u8* dstPhys = nullptr;
    usz dstOffset = 0;
    for (usz i = 0; i < _state->regions.size(); ++i) {
        MemoryRegion& r = _state->regions[i];
        if (dest >= r.base && dest < r.base + r.size) {
            dstPhys = r.physical;
            dstOffset = dest - r.base;
            break;
        }
    }

    if (srcPhys && dstPhys) {
        u8* src = srcPhys + srcOffset;
        u8* dst = dstPhys + dstOffset;
        for (usz i = 0; i < length; ++i) {
            dst[i] = src[i];
        }
    }
}

void Task::alloc(usz dest, usz length) {
    if (!_state || length == 0) return;

    u8* mem = new u8[length];
    // Zero-initialize.
    for (usz i = 0; i < length; ++i) mem[i] = 0;

    MemoryRegion region;
    region.base = dest;
    region.size = length;
    region.physical = mem;
    region.writable = true;
    region.executable = true;
    region.owned = true;

    _state->regions.push(region);
}

void Task::mmap(usz source, usz dest, usz length) {
    if (!_state || length == 0) return;

    // Find the physical memory backing `source`.
    u8* srcPhys = nullptr;
    usz srcOff = 0;
    for (usz i = 0; i < _state->regions.size(); ++i) {
        MemoryRegion& r = _state->regions[i];
        if (source >= r.base && source < r.base + r.size) {
            srcPhys = r.physical;
            srcOff = source - r.base;
            break;
        }
    }

    // Also check parent/shared tasks' regions.
    if (!srcPhys && _tasker) {
        // Walk shared tasks.
        for (usz s = 0; s < _state->sharedIds.size(); ++s) {
            Task shared = _tasker->findTask(_state->sharedIds[s]);
            if (!shared.valid()) continue;
            for (usz i = 0; i < shared._state->regions.size(); ++i) {
                MemoryRegion& r = shared._state->regions[i];
                if (source >= r.base && source < r.base + r.size) {
                    srcPhys = r.physical;
                    srcOff = source - r.base;
                    break;
                }
            }
            if (srcPhys) break;
        }
    }

    if (!srcPhys) return;

    MemoryRegion region;
    region.base = dest;
    region.size = length;
    region.physical = srcPhys + srcOff;
    region.writable = false;     // Mapped memory is read-only by default.
    region.executable = false;
    region.owned = false;        // Don't free — someone else owns it.

    _state->regions.push(region);
}

void Task::unmap(usz dest, usz length) {
    if (!_state) return;

    for (long long i = (long long)_state->regions.size() - 1; i >= 0; --i) {
        MemoryRegion& r = _state->regions[(usz)i];
        // Check overlap.
        if (r.base < dest + length && r.base + r.size > dest) {
            if (r.owned) {
                delete[] r.physical;
            }
            // Remove by swap-and-pop.
            usz last = _state->regions.size() - 1;
            if ((usz)i != last) {
                _state->regions[(usz)i] = _state->regions[last];
            }
            _state->regions.pop();
        }
    }
}

// -------------------------------------------------------------------------
// IPC
// -------------------------------------------------------------------------

void Task::send(Task& receiver, const String& payload) {
    if (!_state || !receiver._state) return;

    Message msg;
    msg.senderId = _state->id;
    msg.payload = payload;
    receiver._state->inbox.push(msg);

    // Sender is automatically shared to receiver.
    bool alreadyShared = false;
    for (usz i = 0; i < receiver._state->sharedIds.size(); ++i) {
        if (receiver._state->sharedIds[i] == _state->id) {
            alreadyShared = true;
            break;
        }
    }
    if (!alreadyShared) {
        receiver._state->sharedIds.push(_state->id);
    }
}

void Task::share(Task& taskObj) {
    if (!_state || !taskObj._state) return;

    bool alreadyShared = false;
    for (usz i = 0; i < _state->sharedIds.size(); ++i) {
        if (_state->sharedIds[i] == taskObj._state->id) {
            alreadyShared = true;
            break;
        }
    }
    if (!alreadyShared) {
        _state->sharedIds.push(taskObj._state->id);
    }
}

// -------------------------------------------------------------------------
// Scheduling
// -------------------------------------------------------------------------

void Task::setQuota(u64 us) {
    if (!_state) return;
    _state->quotaUs = us;
}

void Task::setMemorySize(usz bytes) {
    if (!_state) return;
    // Only meaningful before start() and before any alloc().
    if (_state->regions.size() == 0 && _state->status == TaskStatus::Created) {
        _state->stackSize = bytes;
    }
}

// -------------------------------------------------------------------------
// Callbacks
// -------------------------------------------------------------------------

void Task::setOnFetch(Func<void(usz, usz)> cb) {
    if (!_state) return;
    _state->onFetch = cb;
}

// -------------------------------------------------------------------------
// Children
// -------------------------------------------------------------------------

Task Task::child(usz index) {
    Task t;
    if (!_state || !_tasker) return t;
    if (index >= _state->childIds.size()) return t;
    return _tasker->findTask(_state->childIds[index]);
}

// -------------------------------------------------------------------------
// Context Entry Trampoline (called by assembly)
// -------------------------------------------------------------------------

// The Xtensa-specific trampoline is in arch/Context_Xtensa.cpp.
// This generic version is used by all other architectures.
#if !defined(__XTENSA__)

extern "C" void xi_context_entry_trampoline(void* arg) {
    (void)arg;
    TaskState* state = xi_get_current_task();
    if (state && state->entryFn) {
        state->entryFn(state->entryArg);
    }
    // Mark as finished.
    if (state) {
        state->status = TaskStatus::Finished;
    }
    // Yield back to scheduler. We can't return from here normally
    // since the context was set up by xi_context_init.
    if (Tasker::instance) {
        usz core = state ? state->currentCore : 0;
        Tasker::instance->yield(core);
    }
    // Should never reach here. Spin if it does.
    for (;;) {}
}

#endif // !defined(__XTENSA__)

} // namespace Execution
