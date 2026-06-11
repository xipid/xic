/**
 * @file Task.cpp
 * @brief Task lifecycle, IPC, and memory operations — portable C++.
 */

#include "../../include/Execution/Task.hpp"
#include "../../include/Execution/Interrupt.hpp"
#include <cstdio>

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
// Static variables for Task global state
// -------------------------------------------------------------------------

Array<TaskState*> Task::_tasks;
Array<CoreState> Task::_cores;
usz Task::_nextTaskId = 1;
u64 Task::_schedulePeriodUs = 10000;
u32 Task::_interruptIntervalUs = 1000;
Func<void(usz, u32)> Task::_onChangeFrequency;
Array<Task::PhysicalAllocation> Task::_allocations;
Task* Task::instance = nullptr;

// -------------------------------------------------------------------------
// Static: current task
// -------------------------------------------------------------------------

static thread_local TaskState* tl_currentTask = nullptr;
thread_local usz xi_last_guest_rbx = 0;

Task Task::current() {
    ensureInitialized();
    Task t;
    if (tl_currentTask) {
        t._state = tl_currentTask;
    } else {
        t = root();
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

static void xi_aot_rewrite_task_regions(TaskState* state) {
    if (!state || !state->isIsolated) return;

    TaskState* prev = xi_get_current_task();
    xi_set_current_task(state);

    for (usz i = 0; i < state->regions.size(); ++i) {
        MemoryRegion& r = state->regions[i];
        if (r.physical && r.executable) {
            AOTRegion* cached = AOT::findCached(state->aotCache, reinterpret_cast<usz>(r.physical), r.size);
            if (!cached) {
                AOTResult res = AOT::rewrite(r.physical, r.size, state->regions, 0);
                if (res.success && res.patchedCode) {
                    AOTRegion reg;
                    reg.originalAddr = reinterpret_cast<usz>(r.physical);
                    reg.originalSize = r.size;
                    reg.patchedCode = res.patchedCode;
                    reg.patchedSize = res.patchedSize;
                    state->aotCache.push(reg);
                    // //::printf("[AOT Debug] Cached physical=0x%lx size=0x%lx to patchedCode=%p size=0x%lx\n",
                    //          (long)reg.originalAddr, (long)reg.originalSize, reg.patchedCode, reg.patchedSize);
                    // //::printf("[AOT Debug] Patched bytes:\n");
                    // for (int j = 0; j < 1024 && j < (int)reg.patchedSize; ++j) {
                    //     ::printf("%02x ", reg.patchedCode[j]);
                    //     if ((j + 1) % 16 == 0) ::printf("\n");
                    // }
                    // ::printf("\n");
                }
            }
        }
    }

    xi_set_current_task(prev);
}

static void* xi_translate_to_patched_address(TaskState* state, void* addr) {
    if (!state || !state->isIsolated || !addr) return addr;
    usz target = reinterpret_cast<usz>(addr);
    for (usz i = 0; i < state->regions.size(); ++i) {
        MemoryRegion& r = state->regions[i];
        if (r.physical && r.executable) {
            // Check virtual address space
            if (target >= r.base && target < r.base + r.size) {
                AOTRegion* cached = AOT::findCached(state->aotCache, reinterpret_cast<usz>(r.physical), r.size);
                if (cached && cached->patchedCode) {
                    usz offset = target - r.base;
                    return cached->patchedCode + offset;
                }
            }
            // Check physical address space
            if (target >= reinterpret_cast<usz>(r.physical) &&
                target < reinterpret_cast<usz>(r.physical) + r.size) {
                AOTRegion* cached = AOT::findCached(state->aotCache, reinterpret_cast<usz>(r.physical), r.size);
                if (cached && cached->patchedCode) {
                    usz offset = target - reinterpret_cast<usz>(r.physical);
                    return cached->patchedCode + offset;
                }
            }
        }
    }
    return addr;
}

static u8* xi_carve_from_parent(TaskState* parent, usz length, bool executable) {
    if (!parent) return nullptr;
    for (usz i = 0; i < parent->regions.size(); ++i) {
        MemoryRegion& pr = parent->regions[i];
        if (!pr.physical) continue;
        if (executable ? !pr.executable : !pr.writable) continue;
        if (pr.size < length) continue;

        for (usz offset = 0; offset <= pr.size - length; offset += 16) {
            u8* candidate = pr.physical + offset;
            bool overlap = false;
            for (usz c = 0; c < parent->childIds.size(); ++c) {
                TaskState* child = Task::_tasks[parent->childIds[c]];
                if (!child) continue;
                for (usz r = 0; r < child->regions.size(); ++r) {
                    MemoryRegion& cr = child->regions[r];
                    if (cr.physical) {
                        if (candidate < cr.physical + cr.size && candidate + length > cr.physical) {
                            overlap = true;
                            break;
                        }
                    }
                }
                if (overlap) break;
            }
            if (!overlap) {
                return candidate;
            }
        }
    }
    return nullptr;
}

static u8* xi_allocate_and_carve(TaskState* state, usz length, bool executable) {
    if (state->id == 0) {
        u8* mem = new u8[length];
        for (usz i = 0; i < length; ++i) mem[i] = 0;
        Task::registerAllocation(mem, length);

        MemoryRegion region;
        region.base = 0;
        region.size = length;
        region.physical = mem;
        region.writable = !executable;
        region.executable = executable;
        region.owned = true;
        state->regions.push(region);
        return mem;
    }

    TaskState* parent = Task::_tasks[state->parentId];
    u8* mem = nullptr;
    if (parent) {
        mem = xi_carve_from_parent(parent, length, executable);
        if (mem) {
            Task::retainAllocation(mem);
        }
    }

    if (!mem) {
        // Fall back to allocating new memory if parent has no suitable regions.
        // Register the allocation, and if a parent exists, add it to the parent's
        // regions list first so the parent retains visibility/write access to it.
        mem = new u8[length];
        for (usz i = 0; i < length; ++i) mem[i] = 0;
        Task::registerAllocation(mem, length);

        if (parent && parent->id != 0) {
            MemoryRegion region;
            region.base = reinterpret_cast<usz>(mem);
            region.size = length;
            region.physical = mem;
            region.writable = true;
            region.executable = executable;
            region.owned = true;
            parent->regions.push(region);

            // Since both parent and child now reference this allocation:
            Task::retainAllocation(mem);
        }
    }
    return mem;
}

// -------------------------------------------------------------------------
// Context Validation (Defense-in-Depth)
// -------------------------------------------------------------------------

/**
 * @brief Validates a task's context before switching to it.
 *
 * Checks:
 *   1. RSP is within the task's allocated stack bounds.
 *   2. RIP is within an executable region or the entry trampoline.
 *
 * This provides a scheduling-boundary containment check that complements
 * the AOT SFI instrumentation. Even if the AOT rewriter has a bug,
 * the scheduler will catch an escaped context before it executes.
 *
 * @return true if the context is valid, false if it must be trapped.
 */
bool xi_validate_context_before_switch(TaskState* state) {
    if (!state) return false;

    // Resolve Stack Pointer (sp) and Instruction Pointer (ip) based on architecture.
    usz sp = 0;
    usz ip = 0;

#if defined(__x86_64__) || defined(_M_X64)
    sp = state->context.rsp;
    ip = state->context.rip;
#elif defined(__riscv) && (__riscv_xlen == 32)
    sp = state->context.x[1]; // x2 is sp
    ip = state->context.pc;
#elif defined(__XTENSA__)
    sp = state->context.a[1]; // a1 is sp
    ip = state->context.pc;
#else
    sp = state->context.sp;
    ip = state->context.pc;
#endif

    // Validate stack pointer.
    if (state->stack && state->stackSize > 0) {
        usz stackBase = reinterpret_cast<usz>(state->stack);
        usz stackTop = stackBase + state->stackSize;
        if (sp < stackBase || sp > stackTop) {
            // Stack pointer outside allocated stack — containment violation.
            state->status = TaskStatus::Destroyed;
            return false;
        }
    }

    u8* ipPtr = reinterpret_cast<u8*>(ip);

    // If isolated, rewrite regions and translate context RIP
    if (state->isIsolated) {
        xi_aot_rewrite_task_regions(state);
        for (usz i = 0; i < state->regions.size(); ++i) {
            MemoryRegion& r = state->regions[i];
            if (r.physical && r.executable) {
                if (ipPtr >= r.physical && ipPtr < r.physical + r.size) {
                    AOTRegion* cached = AOT::findCached(state->aotCache, reinterpret_cast<usz>(r.physical), r.size);
                    if (cached && cached->patchedCode) {
                        usz offset = ipPtr - r.physical;
                        usz newRip = reinterpret_cast<usz>(cached->patchedCode) + offset;
#if defined(__x86_64__) || defined(_M_X64)
                        state->context.rip = newRip;
#else
                        state->context.pc = newRip;
#endif
                        ipPtr = reinterpret_cast<u8*>(newRip);
                        break;
                    }
                }
            }
        }
    }

    // Allow the entry trampoline (kernel-managed).
    u8* trampolineAddr = reinterpret_cast<u8*>(xi_context_entry_trampoline);
    if (ipPtr >= trampolineAddr && ipPtr < trampolineAddr + 4096) {
        return true; // Entry trampoline is always valid.
    }

    // Allow the task's entry function (set by the scheduler).
    if (state->entryFn) {
        u8* entryAddr = reinterpret_cast<u8*>(state->entryFn);
        if (ipPtr >= entryAddr && ipPtr < entryAddr + 256) {
            return true; // Entry function range is valid (tight bound).
        }
    }

    // Check executable regions.
    for (usz i = 0; i < state->regions.size(); ++i) {
        MemoryRegion& r = state->regions[i];
        if (r.physical && r.executable) {
            if (ipPtr >= r.physical && ipPtr < r.physical + r.size) {
                return true;
            }
        }
    }

    // Check AOT cache.
    for (usz i = 0; i < state->aotCache.size(); ++i) {
        AOTRegion& aot = state->aotCache[i];
        if (aot.patchedCode) {
            if (ipPtr >= aot.patchedCode && ipPtr < aot.patchedCode + aot.patchedSize) {
                return true;
            }
        }
    }

    // For non-isolated tasks, walk parent chain and check their regions.
    if (!state->isIsolated) {
        usz parentId = state->parentId;
        while (parentId < Task::_tasks.size() && Task::_tasks[parentId]) {
            TaskState* parent = Task::_tasks[parentId];
            for (usz i = 0; i < parent->regions.size(); ++i) {
                MemoryRegion& r = parent->regions[i];
                if (r.physical && r.executable) {
                    if (ipPtr >= r.physical && ipPtr < r.physical + r.size) {
                        return true;
                    }
                }
            }
            for (usz i = 0; i < parent->aotCache.size(); ++i) {
                AOTRegion& aot = parent->aotCache[i];
                if (aot.patchedCode && ipPtr >= aot.patchedCode &&
                    ipPtr < aot.patchedCode + aot.patchedSize) {
                    return true;
                }
            }
            // Allow parent's entry function too.
            if (parent->entryFn) {
                u8* parentEntry = reinterpret_cast<u8*>(parent->entryFn);
                if (ipPtr >= parentEntry && ipPtr < parentEntry + 256) {
                    return true;
                }
            }
            if (parentId == parent->parentId) break; // root
            parentId = parent->parentId;
        }
        // Also allow stack-based trampolines (the task runs on host stack).
        if (state->stack && ipPtr >= state->stack &&
            ipPtr < state->stack + state->stackSize) {
            return true;
        }
        // Non-isolated C++ function tasks: allow the host code range.
        // These are trusted tasks (the parent explicitly gave them a C++ function).
        // The entry function + its entire compilation unit is accessible.
        return true;
    }

    // Isolated task with IP outside all valid regions — violation.
    state->status = TaskStatus::Destroyed;
    return false;
}

static void xi_context_switch_validated(TaskContext* from, TaskState* toState) {
    if (toState) {
        if (!xi_validate_context_before_switch(toState)) {
            std::abort();
        }
        xi_context_switch(from, &toState->context);
    }
}

static void xi_context_switch_validated_core(TaskContext* from, TaskState* toState, CoreState& core) {
    if (!toState) {
        ::printf("[Context Switch] Switching to idle context\n"); ::fflush(stdout);
        xi_context_switch(from, &core.idleContext);
        return;
    }
    ::printf("[Context Switch] Validating toState id=%lu, rip=0x%lx, rsp=0x%lx\n", 
              (unsigned long)toState->id, (unsigned long)toState->context.rip, (unsigned long)toState->context.rsp);
    ::fflush(stdout);
    if (!xi_validate_context_before_switch(toState)) {
        ::printf("[Context Switch] Validation failed for task %lu!\n", (unsigned long)toState->id); ::fflush(stdout);
        toState->status = TaskStatus::Destroyed;
        core.currentTaskId = 0;
        xi_set_current_task(nullptr);
        xi_context_switch(from, &core.idleContext);
        return;
    }
    ::printf("[Context Switch] Switching to task %lu, rip=0x%lx\n", (unsigned long)toState->id, (unsigned long)toState->context.rip);
    ::fflush(stdout);
    xi_context_switch(from, &toState->context);
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
    if (!_state) return child;

    Task caller = Task::current();
    if (caller.valid() && caller.id() != 0) {
        if (_state->id != caller.id()) {
            return child; // Blocked: can only spawn under self.
        }
    }

    // Fork bomb protection: if parent has limited quota, check capacity.
    u64 childQuota = _state->minChildQuotaUs;
    if (_state->quotaUs > 0 && childQuota > 0) {
        // Parent has limited quota — child quota is subtracted from parent capacity.
        if (_state->childQuotaUsed + childQuota > _state->quotaUs) {
            return child; // Blocked: parent quota exhausted by children.
        }
    }

    TaskState* cs = Task::allocTask(_state->id);
    if (!cs) return child;

    child._state = cs;

    // Apply fork bomb protection: child gets minimum quota.
    if (childQuota > 0) {
        cs->quotaUs = childQuota;
        if (_state->quotaUs > 0) {
            _state->childQuotaUsed += childQuota;
        }
    }

    // Inherit parent's minChildQuotaUs — cascading protection.
    cs->minChildQuotaUs = _state->minChildQuotaUs;

    _state->childIds.push(cs->id);

    // Parent is always implicitly shared to child.
    cs->sharedIds.push(_state->id);

    return child;
}

void Task::resume() {
    if (!_state) return;

    Task caller = Task::current();
    if (caller.valid()) {
        if (_state->id != caller.id() && _state->parentId != caller.id()) {
            return; // Blocked: only self or parent can wake/start a task.
        }
    }

    // Auto-allocate memory if none has been allocated.
    if (_state->regions.size() == 0) {
        usz memSize = _state->stackSize > 0 ? _state->stackSize : XI_DEFAULT_TASK_MEM;
        alloc(0, memSize);
    }

    // Allocate a context stack if not yet done (carved from parent memory).
    if (!_state->stack) {
        usz stackSz = getStackSize();
        _state->stack = xi_allocate_and_carve(_state, stackSz, false);
        _state->stackSize = stackSz;
        _state->stackOwned = (_state->id == 0);
    }

    if (_state->status == TaskStatus::Created) {
        // First start: initialize context.
        if (_state->isIsolated) {
            xi_aot_rewrite_task_regions(_state);
            if (_state->entryFn) {
                _state->entryFn = (void(*)(void*))xi_translate_to_patched_address(_state, (void*)_state->entryFn);
            }
        }
        if (_state->entryFn) {
            xi_context_init(&_state->context, _state->entryFn, _state->entryArg,
                            _state->stack, _state->stackSize);
        }
        _state->status = TaskStatus::Ready;
        Task::enqueue(_state->id);
    } else if (_state->status == TaskStatus::Paused ||
               _state->status == TaskStatus::Sleeping) {
        // Resume.
        _state->status = TaskStatus::Ready;
        Task::enqueue(_state->id);
    }
}

void Task::stop() {
    if (!_state) return;
    Task caller = Task::current();
    if (caller.valid() && caller.id() != 0) {
        if (_state->id != caller.id() && _state->parentId != caller.id()) {
            return; // Blocked: not self and not child.
        }
    }
    _state->status = TaskStatus::Paused;
    // If this is the currently running task, yield.
    usz core = _state->currentCore;
    if (Task::currentTask(core) == _state) {
        Task::current().yield(core);
    }
}

void Task::stop(u64 us) {
    if (!_state) return;
    Task caller = Task::current();
    if (caller.valid() && caller.id() != 0) {
        if (_state->id != caller.id() && _state->parentId != caller.id()) {
            return; // Blocked: not self and not child.
        }
    }
    _state->status = TaskStatus::Sleeping;
    _state->sleepUntilUs = xi_micros_now() + us;
    usz core = _state->currentCore;
    if (Task::currentTask(core) == _state) {
        Task::current().yield(core);
    }
}

bool xi_is_task_dead_recursive(usz tid) {
    if (tid >= Task::_tasks.size()) return true;
    TaskState* ts = Task::_tasks[tid];
    if (!ts) return true;
    if (ts->status == TaskStatus::Finished || ts->status == TaskStatus::Destroyed) {
        for (usz i = 0; i < ts->childIds.size(); ++i) {
            if (!xi_is_task_dead_recursive(ts->childIds[i])) {
                return false;
            }
        }
        return true;
    }
    return false;
}

static bool xi_check_wait_dead_condition(TaskState* ts) {
    if (!ts || ts->waitDeadTarget == 0) return false;
    if (ts->waitDeadTarget == (usz)-2) {
        for (usz i = 0; i < ts->childIds.size(); ++i) {
            if (!xi_is_task_dead_recursive(ts->childIds[i])) {
                return false;
            }
        }
        return true;
    } else if (ts->waitDeadTarget == (usz)-1) {
        for (usz i = 1; i < Task::_tasks.size(); ++i) {
            if (i == ts->id) continue;
            if (Task::_tasks[i] && !xi_is_task_dead_recursive(i)) {
                return false;
            }
        }
        return true;
    } else {
        return xi_is_task_dead_recursive(ts->waitDeadTarget);
    }
}

void Task::_execute_impl_raw(int mode, usz targetId, void (*fn)(void*), void* arg) {
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

    if (!_state->stack && fn) {
        usz stackSz = getStackSize();
        _state->stack = xi_allocate_and_carve(_state, stackSz, false);
        _state->stackSize = stackSz;
        _state->stackOwned = (_state->id == 0);
    }

    if (fn) {
        if (_state->isIsolated) {
            xi_aot_rewrite_task_regions(_state);
            void* origFn = (void*)fn;
            fn = (void(*)(void*))xi_translate_to_patched_address(_state, (void*)fn);
            //::printf("[AOT Debug] Translated entry point %p to %p\n", origFn, fn);
        }
        _state->entryFn = fn;
        _state->entryArg = arg;
        xi_context_init(&_state->context, _state->entryFn, _state->entryArg, _state->stack, _state->stackSize);
    }

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
                        xi_context_switch(&_state->context, &core.idleContext);
                    } else {
                        next->status = TaskStatus::Running;
                        core.currentTaskId = next->id;
                        xi_set_current_task(next);
                        xi_context_switch(&_state->context, &next->context);
                    }
                } else {
                    core.currentTaskId = 0;
                    xi_set_current_task(nullptr);
                    xi_context_switch(&_state->context, &core.idleContext);
                }
            }
        }
    } else {
        if (mode == 0) {
            _state->status = TaskStatus::Ready;
            Task::enqueue(_state->id);
        }
    }
    ::printf("[_execute_impl_raw] Completed! target status=%d\n", (int)_state->status);
    ::fflush(stdout);

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

void Task::destroy() {
    if (!_state) return;
    Task caller = Task::current();
    if (caller.valid() && caller.id() != 0) {
        if (_state->parentId != caller.id()) {
            return; // Blocked: can only destroy own children.
        }
    }
    Task::destroyTask(_state->id);
    _state = nullptr;
}

// -------------------------------------------------------------------------
// Memory
// -------------------------------------------------------------------------

void Task::translate(const MemoryTranslation& mt) {
    if (!_state) return;
    Task caller = Task::current();
    if (caller.valid() && caller.id() != 0) {
        if (_state->parentId != caller.id()) {
            return; // Blocked: only parent can modify memory mappings.
        }
    }
    _state->translations.push(mt);
}

void Task::copy(usz source, usz dest, usz length) {
    if (!_state || length == 0) return;
    Task caller = Task::current();
    if (caller.valid() && caller.id() != 0) {
        if (_state->parentId != caller.id()) {
            return; // Blocked: only parent can copy memory.
        }
    }

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
    Task caller = Task::current();
    if (caller.valid() && caller.id() != 0) {
        if (_state->parentId != caller.id() && _state->id != caller.id()) {
            return; // Blocked: only parent or self can allocate memory.
        }
    }

    u8* mem = xi_allocate_and_carve(_state, length, false);
    if (!mem) return;

    MemoryRegion region;
    region.base = dest;
    region.size = length;
    region.physical = mem;
    region.writable = true;
    region.executable = true; // All memory is writable and executable
    region.owned = true;

    _state->regions.push(region);
}



void Task::map(usz source, usz dest, usz length) {
    if (!_state || length == 0) return;

    // A task cannot map memory for itself (Task::current() == self is blocked).
    Task caller = Task::current();
    if (caller.valid() && _state->id == caller.id()) {
        return; // Blocked: self cannot map memory.
    }

    if (caller.valid() && caller.id() != 0) {
        if (_state->parentId != caller.id()) {
            return; // Blocked: only parent can map memory.
        }
    }

    // An isolated task cannot have new memory mapped into it except by parent
    // (which is already enforced above).

    // When a parent maps memory for a child, it maps from the CALLER's
    // (parent's) own memory, not from arbitrary/top memory.
    // Find the physical memory backing `source` in the caller's regions.
    u8* srcPhys = nullptr;
    usz srcOff = 0;

    // Search caller's (parent's) regions first.
    if (caller.valid() && caller._state) {
        for (usz i = 0; i < caller._state->regions.size(); ++i) {
            MemoryRegion& r = caller._state->regions[i];
            if (source >= r.base && source < r.base + r.size) {
                srcPhys = r.physical;
                srcOff = source - r.base;
                break;
            }
        }
    }

    // Also check caller's children's regions (accessible to caller since parent owns child memory).
    if (!srcPhys && caller.valid() && caller._state) {
        for (usz c = 0; c < caller._state->childIds.size(); ++c) {
            Task child = Task::findTask(caller._state->childIds[c]);
            if (!child.valid()) continue;
            for (usz i = 0; i < child._state->regions.size(); ++i) {
                MemoryRegion& r = child._state->regions[i];
                if (source >= r.base && source < r.base + r.size) {
                    srcPhys = r.physical;
                    srcOff = source - r.base;
                    break;
                }
            }
            if (srcPhys) break;
        }
    }

    // If not found, also check the target task's own regions.
    if (!srcPhys) {
        for (usz i = 0; i < _state->regions.size(); ++i) {
            MemoryRegion& r = _state->regions[i];
            if (source >= r.base && source < r.base + r.size) {
                srcPhys = r.physical;
                srcOff = source - r.base;
                break;
            }
        }
    }

    // Also check target's shared tasks' regions (accessible to target).
    if (!srcPhys) {
        for (usz s = 0; s < _state->sharedIds.size(); ++s) {
            Task shared = Task::findTask(_state->sharedIds[s]);
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

    // Also check shared tasks' regions (accessible to caller).
    if (!srcPhys && caller.valid() && caller._state) {
        for (usz s = 0; s < caller._state->sharedIds.size(); ++s) {
            Task shared = Task::findTask(caller._state->sharedIds[s]);
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

    // If caller is root (kernel context), search all tasks in the system as fallback.
    if (!srcPhys && caller.valid() && caller.id() == 0) {
        for (usz t = 0; t < Task::_tasks.size(); ++t) {
            TaskState* ts = Task::_tasks[t];
            if (!ts) continue;
            for (usz i = 0; i < ts->regions.size(); ++i) {
                MemoryRegion& r = ts->regions[i];
                if (source >= r.base && source < r.base + r.size) {
                    srcPhys = r.physical;
                    srcOff = source - r.base;
                    break;
                }
            }
            if (srcPhys) break;
        }
    }

    // If caller is not set (kernel context), search target's regions as fallback.
    if (!srcPhys && !caller.valid()) {
        for (usz i = 0; i < _state->regions.size(); ++i) {
            MemoryRegion& r = _state->regions[i];
            if (source >= r.base && source < r.base + r.size) {
                srcPhys = r.physical;
                srcOff = source - r.base;
                break;
            }
        }
    }

    if (!srcPhys) return;

    Task::retainAllocation(srcPhys + srcOff);

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
    Task caller = Task::current();
    if (caller.valid() && caller.id() != 0) {
        if (_state->parentId != caller.id()) {
            return; // Blocked: only parent can unmap memory.
        }
    }

    for (long long i = (long long)_state->regions.size() - 1; i >= 0; --i) {
        MemoryRegion& r = _state->regions[(usz)i];
        // Check overlap.
        if (r.base < dest + length && r.base + r.size > dest) {
            if (r.physical) {
                Task::releaseAllocation(r.physical);
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

void Task::unmap() {
    if (!_state) return;
    Task caller = Task::current();
    if (caller.valid() && caller.id() != 0) {
        if (_state->parentId != caller.id()) {
            return; // Blocked: only parent can unmap all memory.
        }
    }

    // Release all memory regions.
    for (usz i = 0; i < _state->regions.size(); ++i) {
        if (_state->regions[i].physical) {
            Task::releaseAllocation(_state->regions[i].physical);
        }
    }
    _state->regions.clear();

    // Clear all memory translations.
    _state->translations.clear();

    // Clear all registered fetch ranges.
    _state->fetchRanges.clear();

    // Invalidate AOT cache since memory layout changed.
    AOT::destroyCache(_state->aotCache);

    // Activate memory isolation — the task now sees its own address space from 0.
    _state->isIsolated = true;
}


// -------------------------------------------------------------------------
// IPC
// -------------------------------------------------------------------------

void Task::send(Task& receiver, const String& payload) {
    if (!_state || !receiver._state) return;

    Task caller = Task::current();
    if (caller.valid() && caller.id() != 0) {
        bool allowed = false;
        if (receiver.id() == caller.id() ||
            receiver.parentId() == caller.id() ||
            receiver.id() == caller.parentId() ||
            receiver.parentId() == caller.parentId()) {
            allowed = true;
        } else {
            for (usz i = 0; i < caller._state->sharedIds.size(); ++i) {
                if (caller._state->sharedIds[i] == receiver.id()) {
                    allowed = true;
                    break;
                }
            }
        }
        if (!allowed) {
            return; // Blocked: receiver escapes the container.
        }
    }

    TaskState* currentTaskState = xi_get_current_task();
    usz senderId = currentTaskState ? currentTaskState->id : _state->id;

    Message msg;
    msg.senderId = senderId;
    msg.payload = payload;
    receiver._state->inbox.push(msg);

    if (receiver._state->status == TaskStatus::Paused && receiver._state->isWaitingForMessage) {
        receiver._state->isWaitingForMessage = false;
        receiver._state->status = TaskStatus::Ready;
    }

    // Sender is automatically shared to receiver.
    bool alreadyShared = false;
    for (usz i = 0; i < receiver._state->sharedIds.size(); ++i) {
        if (receiver._state->sharedIds[i] == senderId) {
            alreadyShared = true;
            break;
        }
    }
    if (!alreadyShared) {
        receiver._state->sharedIds.push(senderId);
    }
}

void Task::share(Task& taskObj) {
    if (!_state || !taskObj._state) return;

    Task caller = Task::current();
    if (caller.valid() && caller.id() != 0) {
        // 1. Target (this) must be self or child of caller.
        if (_state->id != caller.id() && _state->parentId != caller.id()) {
            return; // Blocked: cannot modify shared task.
        }
        // 2. taskObj must be reachable by caller.
        bool reachable = false;
        if (taskObj.id() == caller.id() ||
            taskObj.parentId() == caller.id() ||
            taskObj.id() == caller.parentId() ||
            taskObj.parentId() == caller.parentId()) {
            reachable = true;
        } else {
            for (usz i = 0; i < caller._state->sharedIds.size(); ++i) {
                if (caller._state->sharedIds[i] == taskObj.id()) {
                    reachable = true;
                    break;
                }
            }
        }
        if (!reachable) {
            return; // Blocked: taskObj escapes the container.
        }
    }

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

    Task caller = Task::current();
    if (caller.valid() && caller.id() != 0) {
        if (_state->parentId != caller.id()) {
            return; // Blocked: only parent can change quota.
        }
    }

    // Adjust core's totalQuotaUs if the task is currently enqueued.
    usz coreId = _state->currentCore;
    CoreState* core = Task::coreState(coreId);
    if (core) {
        bool enqueued = false;
        for (usz i = 0; i < core->runQueue.size(); ++i) {
            if (core->runQueue[i] == _state->id) {
                enqueued = true;
                break;
            }
        }
        if (enqueued) {
            // Safely update cumulative quota load.
            core->totalQuotaUs = (core->totalQuotaUs - _state->quotaUs) + us;
        }
    }

    _state->quotaUs = us;
}

void Task::setMemorySize(usz bytes) {
    if (!_state) return;
    Task caller = Task::current();
    if (caller.valid() && caller.id() != 0) {
        if (_state->parentId != caller.id()) {
            return; // Blocked: only parent can set memory size.
        }
    }
    // Only meaningful before start() and before any alloc().
    if (_state->regions.size() == 0 && _state->status == TaskStatus::Created) {
        _state->stackSize = bytes;
    }
}

// -------------------------------------------------------------------------
// Core Management
// -------------------------------------------------------------------------

void Task::setup(usz coreId, bool startTimer) {
    Task caller = Task::current();
    if (caller.valid() && (caller.id() != 0 || caller.parentId() != 0)) {
        return; // Child tasks cannot modify cores
    }
    ensureInitialized();
    while (_cores.size() <= coreId) {
        CoreState cs;
        cs.id = _cores.size();
        _cores.push(cs);
    }

    CoreState& core = _cores[coreId];
    if (core.enabled) return;

    core.id = coreId;
    core.enabled = true;
    core.currentTaskId = 0; // Root task.

    if (startTimer) {
        xi_timer_start(coreId, _interruptIntervalUs);
    }

    // Auto-enqueue any Ready tasks that are not yet enqueued
    for (usz i = 1; i < _tasks.size(); ++i) {
        TaskState* s = _tasks[i];
        if (s && s->status == TaskStatus::Ready) {
            bool alreadyEnqueued = false;
            for (usz c = 0; c < _cores.size(); ++c) {
                if (_cores[c].enabled) {
                    for (usz r = 0; r < _cores[c].runQueue.size(); ++r) {
                        if (_cores[c].runQueue[r] == s->id) {
                            alreadyEnqueued = true;
                            break;
                        }
                    }
                }
                if (alreadyEnqueued) break;
            }
            if (!alreadyEnqueued) {
                Task::enqueue(s->id);
            }
        }
    }
}

void Task::disable(usz coreId) {
    Task caller = Task::current();
    if (caller.valid() && (caller.id() != 0 || caller.parentId() != 0)) {
        return; // Child tasks cannot modify cores
    }
    ensureInitialized();
    if (coreId >= _cores.size()) return;
    CoreState& core = _cores[coreId];
    if (!core.enabled) return;

    xi_timer_stop(coreId);

    // Migrate currently running task if not pinned
    if (core.currentTaskId != 0) {
        Task t = findTask(core.currentTaskId);
        if (t.valid() && !t._state->isPinned) {
            t._state->status = TaskStatus::Ready;
            usz newCore = assignCore(core.currentTaskId);
            if (newCore != coreId && newCore < _cores.size()) {
                bool alreadyInQueue = false;
                for (usz j = 0; j < _cores[newCore].runQueue.size(); ++j) {
                    if (_cores[newCore].runQueue[j] == core.currentTaskId) {
                        alreadyInQueue = true;
                        break;
                    }
                }
                if (!alreadyInQueue) {
                    _cores[newCore].runQueue.push(core.currentTaskId);
                }
                t._state->currentCore = newCore;
                _cores[newCore].totalQuotaUs += t._state->quotaUs;
            }
        }
    }

    // Migrate other unpinned tasks in runQueue to other enabled cores.
    for (usz i = 0; i < core.runQueue.size(); ++i) {
        usz taskId = core.runQueue[i];
        if (taskId == core.currentTaskId) continue;
        Task t = findTask(taskId);
        if (!t.valid()) continue;
        if (!t._state->isPinned) {
            usz newCore = assignCore(taskId);
            if (newCore != coreId && newCore < _cores.size()) {
                bool alreadyInQueue = false;
                for (usz j = 0; j < _cores[newCore].runQueue.size(); ++j) {
                    if (_cores[newCore].runQueue[j] == taskId) {
                        alreadyInQueue = true;
                        break;
                    }
                }
                if (!alreadyInQueue) {
                    _cores[newCore].runQueue.push(taskId);
                }
                t._state->currentCore = newCore;
                _cores[newCore].totalQuotaUs += t._state->quotaUs;
            }
        }
    }

    core.runQueue.clear();
    core.enabled = false;
    core.currentTaskId = 0;
}

void Task::setFrequencySlider(usz coreId, u32 minFreq, u32 maxFreq) {
    Task caller = Task::current();
    if (caller.valid() && (caller.id() != 0 || caller.parentId() != 0)) {
        return; // Child tasks cannot modify cores
    }
    ensureInitialized();
    while (_cores.size() <= coreId) {
        CoreState cs;
        cs.id = _cores.size();
        _cores.push(cs);
    }
    _cores[coreId].minFreq = minFreq;
    _cores[coreId].maxFreq = maxFreq;
}



void Task::OnChangeFrequencyProxy::operator=(Func<void(usz, u32)> cb) {
    Task caller = Task::current();
    if (caller.valid() && (caller.id() != 0 || caller.parentId() != 0)) {
        return; // Child tasks cannot modify frequency callback
    }
    Task::_onChangeFrequency = cb;
}

Task::OnChangeFrequencyProxy::operator bool() const {
    return (bool)Task::_onChangeFrequency;
}

void Task::OnChangeFrequencyProxy::operator()(usz coreId, u32 proposedFreq) const {
    if (Task::_onChangeFrequency) {
        Task::_onChangeFrequency(coreId, proposedFreq);
    }
}

// -------------------------------------------------------------------------
// Callbacks
// -------------------------------------------------------------------------

void Task::setOnFetch(Func<void(usz, usz)> cb) {
    setOnFetch(0, (usz)-1, cb);
}

void Task::setOnFetch(usz start, usz end, Func<void(usz, usz)> cb) {
    if (!_state) return;
    Task caller = Task::current();
    if (caller.valid() && caller.id() != 0) {
        if (_state->parentId != caller.id()) {
            return; // Blocked: only parent can set callbacks.
        }
    }
    TaskState::FetchRange fr;
    fr.start = start;
    fr.end = end;
    fr.callback = cb;
    fr.cached = false;
    fr.resolved = false;
    _state->fetchRanges.push(fr);
}

void Task::setOnFetchCached(usz start, usz end, Func<void(usz, usz)> cb) {
    if (!_state) return;
    Task caller = Task::current();
    if (caller.valid() && caller.id() != 0) {
        if (_state->parentId != caller.id()) {
            return; // Blocked: only parent can set callbacks.
        }
    }
    TaskState::FetchRange fr;
    fr.start = start;
    fr.end = end;
    fr.callback = cb;
    fr.cached = true;
    fr.resolved = false;
    _state->fetchRanges.push(fr);
}

void Task::uncache(usz start, usz end) {
    if (!_state) return;
    Task caller = Task::current();
    if (caller.valid() && caller.id() != 0) {
        if (_state->parentId != caller.id() && _state->id != caller.id()) {
            return; // Blocked: only self or parent can uncache.
        }
    }
    // Reset resolved state in fetchRanges
    for (usz i = 0; i < _state->fetchRanges.size(); ++i) {
        TaskState::FetchRange& fr = _state->fetchRanges[i];
        if (fr.start >= start && fr.end <= end) {
            fr.resolved = false;
        }
    }
    // Unmap the region directly (bypassing parent check in Task::unmap)
    usz length = end - start;
    for (long long i = (long long)_state->regions.size() - 1; i >= 0; --i) {
        MemoryRegion& r = _state->regions[(usz)i];
        if (r.base < start + length && r.base + r.size > start) {
            if (r.physical) {
                Task::releaseAllocation(r.physical);
            }
            usz last = _state->regions.size() - 1;
            if ((usz)i != last) {
                _state->regions[(usz)i] = _state->regions[last];
            }
            _state->regions.pop();
        }
    }
}

void Task::uncache() {
    if (!_state) return;
    Task caller = Task::current();
    if (caller.valid() && caller.id() != 0) {
        if (_state->parentId != caller.id() && _state->id != caller.id()) {
            return; // Blocked: only self or parent can uncache.
        }
    }
    for (usz i = 0; i < _state->fetchRanges.size(); ++i) {
        TaskState::FetchRange& fr = _state->fetchRanges[i];
        fr.resolved = false;
        // Unmap range directly
        usz start = fr.start;
        usz length = fr.end - fr.start;
        for (long long j = (long long)_state->regions.size() - 1; j >= 0; --j) {
            MemoryRegion& r = _state->regions[(usz)j];
            if (r.base < start + length && r.base + r.size > start) {
                if (r.physical) {
                    Task::releaseAllocation(r.physical);
                }
                usz last = _state->regions.size() - 1;
                if ((usz)j != last) {
                    _state->regions[(usz)j] = _state->regions[last];
                }
                _state->regions.pop();
            }
        }
    }
}

// -------------------------------------------------------------------------
// Children
// -------------------------------------------------------------------------

Task Task::child(usz index) {
    Task t;
    if (!_state) return t;
    if (index >= _state->childIds.size()) return t;
    return Task::findTask(_state->childIds[index]);
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
    usz core = state ? state->currentCore : 0;
    Task::current().yield(core);

    // Should never reach here. Spin if it does.
    for (;;) {}
}

#endif // !defined(__XTENSA__)

Task Task::root() {
    ensureInitialized();
    Task t;
    if (_tasks.size() > 0 && _tasks[0]) {
        t._state = _tasks[0];
    }
    return t;
}

Task Task::findTask(usz id) {
    ensureInitialized();
    Task t;
    if (id < _tasks.size() && _tasks[id]) {
        t._state = _tasks[id];
    }
    return t;
}

usz Task::taskCount() {
    ensureInitialized();
    return _tasks.size();
}

void Task::registerAllocation(u8* ptr, usz size) {
    if (!ptr) return;
    ensureInitialized();
    PhysicalAllocation alloc;
    alloc.ptr = ptr;
    alloc.size = size;
    alloc.refCount = 1;
    _allocations.push(alloc);
}

void Task::retainAllocation(u8* ptr) {
    if (!ptr) return;
    ensureInitialized();
    for (usz i = 0; i < _allocations.size(); ++i) {
        PhysicalAllocation& alloc = _allocations[i];
        if (ptr >= alloc.ptr && ptr < alloc.ptr + alloc.size) {
            alloc.refCount++;
            return;
        }
    }
}

void Task::releaseAllocation(u8* ptr) {
    if (!ptr) return;
    ensureInitialized();
    for (usz i = 0; i < _allocations.size(); ++i) {
        PhysicalAllocation& alloc = _allocations[i];
        if (ptr >= alloc.ptr && ptr < alloc.ptr + alloc.size) {
            alloc.refCount--;
            if (alloc.refCount == 0) {
                delete[] alloc.ptr;
                
                // Swap-and-pop.
                usz last = _allocations.size() - 1;
                if (i != last) {
                    _allocations[i] = _allocations[last];
                }
                _allocations.pop();
            }
            return;
        }
    }
    delete[] ptr;
}

TaskState* Task::allocTask(usz parentId) {
    ensureInitialized();
    TaskState* s = new TaskState();
    s->id = _nextTaskId++;
    s->parentId = parentId;
    s->status = TaskStatus::Created;
    s->quotaUs = 0;

    while (_tasks.size() <= s->id) {
        _tasks.push(nullptr);
    }
    _tasks[s->id] = s;

    return s;
}

void Task::enqueue(usz taskId) {
    ensureInitialized();
    if (taskId >= _tasks.size() || !_tasks[taskId]) return;
    TaskState* s = _tasks[taskId];

    usz coreId = 0;
    if (s->isPinned) {
        coreId = s->pinnedCore;
    } else {
        coreId = assignCore(taskId);
    }

    if (coreId >= _cores.size() || !_cores[coreId].enabled) {
        for (usz i = 0; i < _cores.size(); ++i) {
            if (_cores[i].enabled) {
                coreId = i;
                break;
            }
        }
    }

    if (coreId >= _cores.size()) return;

    s->currentCore = coreId;

    CoreState& core = _cores[coreId];
    for (usz i = 0; i < core.runQueue.size(); ++i) {
        if (core.runQueue[i] == taskId) return;
    }
    core.runQueue.push(taskId);

    core.totalQuotaUs += s->quotaUs;
}

void Task::dequeue(usz taskId) {
    ensureInitialized();
    for (usz c = 0; c < _cores.size(); ++c) {
        CoreState& core = _cores[c];
        for (long long i = (long long)core.runQueue.size() - 1; i >= 0; --i) {
            if (core.runQueue[(usz)i] == taskId) {
                usz last = core.runQueue.size() - 1;
                if ((usz)i != last) {
                    core.runQueue[(usz)i] = core.runQueue[last];
                }
                core.runQueue.pop();

                if (taskId < _tasks.size() && _tasks[taskId]) {
                    core.totalQuotaUs -= _tasks[taskId]->quotaUs;
                }
                return;
            }
        }
    }
}

void Task::destroyTask(usz taskId) {
    ensureInitialized();
    if (taskId >= _tasks.size() || !_tasks[taskId]) return;
    TaskState* s = _tasks[taskId];

    Array<usz> childIds = s->childIds;
    for (usz i = 0; i < childIds.size(); ++i) {
        destroyTask(childIds[i]);
    }

    dequeue(taskId);

    if (s->parentId < _tasks.size() && _tasks[s->parentId]) {
        TaskState* parent = _tasks[s->parentId];
        // Return child quota to parent's capacity.
        if (parent->quotaUs > 0 && s->quotaUs > 0 && parent->childQuotaUsed >= s->quotaUs) {
            parent->childQuotaUsed -= s->quotaUs;
        }
        for (long long i = (long long)parent->childIds.size() - 1; i >= 0; --i) {
            if (parent->childIds[(usz)i] == taskId) {
                usz last = parent->childIds.size() - 1;
                if ((usz)i != last) {
                    parent->childIds[(usz)i] = parent->childIds[last];
                }
                parent->childIds.pop();
                break;
            }
        }
    }

    for (usz i = 0; i < s->regions.size(); ++i) {
        if (s->regions[i].physical) {
            releaseAllocation(s->regions[i].physical);
        }
    }

    AOT::destroyCache(s->aotCache);

    if (s->stackOwned && s->stack) {
        releaseAllocation(s->stack);
    }

    s->status = TaskStatus::Destroyed;
    delete s;
    _tasks[taskId] = nullptr;
}

void Task::yield(usz coreId) {
    ::printf("[Task::yield] entered coreId=%lu\n", (unsigned long)coreId);
    ::fflush(stdout);
    ensureInitialized();

    // Wake tasks waiting for death.
    for (usz i = 0; i < _tasks.size(); ++i) {
        TaskState* ts = _tasks[i];
        if (ts && ts->status == TaskStatus::Paused && ts->waitDeadTarget != 0) {
            if (xi_check_wait_dead_condition(ts)) {
                ts->status = TaskStatus::Ready;
                ts->waitDeadTarget = 0;
            }
        }
    }

    TaskState* caller = xi_get_current_task();
    if (caller) {
        // Auto-uncaching: unmap any regions corresponding to non-cached onFetch ranges
        for (usz i = 0; i < caller->fetchRanges.size(); ++i) {
            TaskState::FetchRange& fr = caller->fetchRanges[i];
            if (!fr.cached) {
                // Unmap this range directly
                usz start = fr.start;
                usz length = fr.end - fr.start;
                for (long long j = (long long)caller->regions.size() - 1; j >= 0; --j) {
                    MemoryRegion& r = caller->regions[(usz)j];
                    if (r.base < start + length && r.base + r.size > start) {
                        if (r.physical) {
                            Task::releaseAllocation(r.physical);
                        }
                        usz last = caller->regions.size() - 1;
                        if ((usz)j != last) {
                            caller->regions[(usz)j] = caller->regions[last];
                        }
                        caller->regions.pop();
                    }
                }
            }
        }
        // 1- A task couldnt yield as another core, only the core they are executing in.
        // Task::yield() with no args uses the current core (forced if in task, or 0 if not).
        coreId = caller->currentCore;
    }

    if (coreId >= _cores.size()) return;
    CoreState& core = _cores[coreId];
    if (!core.enabled) return;

    TaskState* current = nullptr;
    if (core.currentTaskId < _tasks.size()) {
        current = _tasks[core.currentTaskId];
    }

    if (!caller) {
        // 2- Outside a task (the top), yield(core) functions as interrupts()...
        
        // Wake sleeping tasks.
        u64 now = (u64)Xi::micros();
        for (usz i = 0; i < core.runQueue.size(); ++i) {
            usz tid = core.runQueue[i];
            if (tid < _tasks.size() && _tasks[tid]) {
                TaskState* ts = _tasks[tid];
                if (ts->status == TaskStatus::Sleeping &&
                    (u64)now >= ts->sleepUntilUs) {
                    ts->status = TaskStatus::Ready;
                }
            }
        }

        // Decrement current task's remaining slice.
        if (current && current->status == TaskStatus::Running) {
            // Count child quota towards parent quota if parent has limited quota (quotaUs > 0)
            usz checkPid = current->parentId;
            while (checkPid > 0) {
                if (checkPid < _tasks.size() && _tasks[checkPid]) {
                    TaskState* parentState = _tasks[checkPid];
                    if (parentState->quotaUs > 0) {
                        if (parentState->remainingUs > _interruptIntervalUs) {
                            parentState->remainingUs -= _interruptIntervalUs;
                        } else {
                            parentState->remainingUs = 0;
                        }
                    }
                    checkPid = parentState->parentId;
                } else {
                    break;
                }
            }

            // Check if any parent's limited quota is exhausted
            bool parentExhausted = false;
            checkPid = current->parentId;
            while (checkPid > 0) {
                if (checkPid < _tasks.size() && _tasks[checkPid]) {
                    TaskState* parentState = _tasks[checkPid];
                    if (parentState->quotaUs > 0 && parentState->remainingUs == 0) {
                        parentExhausted = true;
                        break;
                    }
                    checkPid = parentState->parentId;
                } else {
                    break;
                }
            }

            if (current->remainingUs > _interruptIntervalUs && !parentExhausted) {
                current->remainingUs -= _interruptIntervalUs;
                // Still has quota — don't switch.
                proposeFrequency(coreId);
                return;
            }
            current->remainingUs = 0;
            current->status = TaskStatus::Ready;
        }

        // Pick next task.
        TaskState* next = pickNext(coreId);

        if (!next) {
            if (current && current->status == TaskStatus::Ready) {
                current->status = TaskStatus::Running;
                resetPeriod(coreId);
            }
            proposeFrequency(coreId);
            return;
        }

        if (next == current) {
            next->status = TaskStatus::Running;
            if (next->remainingUs == 0) {
                resetPeriod(coreId);
            }
            proposeFrequency(coreId);
            return;
        }

        // Context switch.
        TaskContext* fromCtx = nullptr;
        if (current) {
            fromCtx = &current->context;
        }

        next->status = TaskStatus::Running;
        next->currentCore = coreId;
        core.currentTaskId = next->id;

        xi_set_current_task(next);

        if (next->remainingUs == 0) {
            resetPeriod(coreId);
        }

        proposeFrequency(coreId);

        xi_context_switch_validated_core(fromCtx ? fromCtx : &core.idleContext, next, core);
    } else {
        // 3- yield inside a task saves it quota... its no longer executing until the next...
        
        if (current && current->status == TaskStatus::Running) {
            current->status = TaskStatus::Ready;
            // saves its quota: we do NOT decrement remainingUs or set it to 0.
        }

        TaskState* next = pickNext(coreId);

        if (!next || next == current) {
            if (!next) {
                if (_tasks.size() > 0 && _tasks[0]) {
                    next = _tasks[0];
                } else if (current && current->status != TaskStatus::Running &&
                           current->status != TaskStatus::Ready) {
                    core.currentTaskId = 0;
                    xi_set_current_task(nullptr);
                    proposeFrequency(coreId);
                    xi_context_switch(&current->context, &core.idleContext);
                    return;
                } else {
                    if (current) {
                        current->status = TaskStatus::Running;
                    }
                    proposeFrequency(coreId);
                    return;
                }
            } else {
                if (current) {
                    current->status = TaskStatus::Running;
                }
                proposeFrequency(coreId);
                return;
            }
        }

        next->status = TaskStatus::Running;
        next->currentCore = coreId;
        core.currentTaskId = next->id;
        xi_set_current_task(next);

        if (next->remainingUs == 0) {
            resetPeriod(coreId);
        }

        proposeFrequency(coreId);

        xi_context_switch_validated_core(current ? &current->context : &core.idleContext, next, core);
    }
}

CoreState* Task::coreState(usz coreId) {
    ensureInitialized();
    if (coreId >= _cores.size()) return nullptr;
    return &_cores[coreId];
}

TaskState* Task::currentTask(usz coreId) {
    ensureInitialized();
    if (coreId >= _cores.size()) return nullptr;
    usz tid = _cores[coreId].currentTaskId;
    if (tid >= _tasks.size()) return nullptr;
    return _tasks[tid];
}

TaskState* Task::pickNext(usz coreId) {
    ensureInitialized();
    if (coreId >= _cores.size()) return nullptr;
    CoreState& core = _cores[coreId];

    if (core.runQueue.size() == 0) return nullptr;

    bool anyReady = false;
    bool anyReadyWithQuota = false;

    for (usz i = 0; i < core.runQueue.size(); ++i) {
        usz tid = core.runQueue[i];
        if (tid >= _tasks.size() || !_tasks[tid]) continue;
        TaskState* ts = _tasks[tid];
        if (ts->status == TaskStatus::Ready) {
            anyReady = true;
            if (ts->remainingUs > 0) {
                anyReadyWithQuota = true;
            }
        }
    }

    if (!anyReady) return nullptr;

    if (!anyReadyWithQuota) {
        resetPeriod(coreId);
    }

    usz bestIdx = core.runQueue.size();
    u64 bestWeight = 0;

    usz startIdx = core.runQueueIndex % core.runQueue.size();
    usz count = core.runQueue.size();

    for (usz n = 0; n < count; ++n) {
        usz idx = (startIdx + n) % count;
        usz tid = core.runQueue[idx];
        if (tid >= _tasks.size() || !_tasks[tid]) continue;

        TaskState* ts = _tasks[tid];

        if (ts->status != TaskStatus::Ready) continue;
        if (ts->remainingUs == 0) continue;

        u64 weight = ts->quotaUs > 0 ? ts->quotaUs : 1;

        if (bestIdx >= core.runQueue.size() || weight > bestWeight) {
            bestWeight = weight;
            bestIdx = idx;
        }
    }

    if (bestIdx >= core.runQueue.size()) {
        return nullptr;
    }

    core.runQueueIndex = (bestIdx + 1) % core.runQueue.size();

    usz selectedId = core.runQueue[bestIdx];
    if (selectedId < _tasks.size()) {
        return _tasks[selectedId];
    }
    return nullptr;
}

usz Task::assignCore(usz taskId) {
    ensureInitialized();
    usz bestCore = 0;
    u64 bestLoad = (u64)-1;
    bool found = false;

    for (usz i = 0; i < _cores.size(); ++i) {
        if (!_cores[i].enabled) continue;
        if (!found || _cores[i].totalQuotaUs < bestLoad) {
            bestCore = i;
            bestLoad = _cores[i].totalQuotaUs;
            found = true;
        }
    }

    (void)taskId;
    return bestCore;
}

void Task::resetPeriod(usz coreId) {
    ensureInitialized();
    if (coreId >= _cores.size()) return;
    CoreState& core = _cores[coreId];

    for (usz i = 0; i < core.runQueue.size(); ++i) {
        usz tid = core.runQueue[i];
        if (tid >= _tasks.size() || !_tasks[tid]) continue;
        TaskState* ts = _tasks[tid];

        if (ts->quotaUs > 0) {
            ts->remainingUs = ts->quotaUs;
        } else {
            ts->remainingUs = _schedulePeriodUs;
        }
    }
}

void Task::proposeFrequency(usz coreId) {
    ensureInitialized();
    if (coreId >= _cores.size()) return;
    CoreState& core = _cores[coreId];

    if (core.maxFreq == 0 || !_onChangeFrequency) return;

    u64 totalDemand = 0;
    usz readyCount = 0;

    for (usz i = 0; i < core.runQueue.size(); ++i) {
        usz tid = core.runQueue[i];
        if (tid >= _tasks.size() || !_tasks[tid]) continue;
        TaskState* ts = _tasks[tid];
        if (ts->status == TaskStatus::Ready || ts->status == TaskStatus::Running) {
            totalDemand += ts->quotaUs > 0 ? ts->quotaUs : _schedulePeriodUs;
            readyCount++;
        }
    }

    u32 proposed;
    if (readyCount == 0) {
        proposed = core.minFreq;
    } else if (totalDemand >= _schedulePeriodUs) {
        proposed = core.maxFreq;
    } else {
        u64 range = core.maxFreq - core.minFreq;
        u64 ratio = (totalDemand * range) / _schedulePeriodUs;
        proposed = core.minFreq + (u32)ratio;
    }

    if (proposed != core.currentProposedFreq) {
        core.currentProposedFreq = proposed;
        _onChangeFrequency(coreId, proposed);
    }
}

void Task::ensureInitialized() {
    if (_tasks.size() == 0) {
        // Create the root task (id=0).
        TaskState* root = new TaskState();
        root->id = 0;
        root->parentId = 0;
        root->status = TaskStatus::Running;
        root->quotaUs = 0; // Unlimited.
        _tasks.push(root);

        static Task rootTask(root);
        instance = &rootTask;
    }
}

void Task::reset() {
    tl_currentTask = nullptr;
    for (usz i = 0; i < 64; ++i) {
        xi_timer_stop(i);
    }

    for (usz i = 0; i < _tasks.size(); ++i) {
        if (_tasks[i]) {
            delete _tasks[i];
        }
    }
    _tasks.clear();

    for (usz i = 0; i < _allocations.size(); ++i) {
        delete[] _allocations[i].ptr;
    }
    _allocations.clear();

    _cores.clear();

    _nextTaskId = 1;
    _schedulePeriodUs = 10000;
    _interruptIntervalUs = 1000;
    _onChangeFrequency._clear();
    instance = nullptr;

    ensureInitialized();
}

void Task::onInstruction(const String& instruction, Func<void()> callback) {
    if (!_state) return;
    Task caller = Task::current();
    bool isSelf = (caller.valid() && _state->id == caller.id());
    bool isParent = (caller.valid() && _state->parentId == caller.id());
    bool isKernel = (!caller.valid() || caller.id() == 0);

    // Self can register callbacks. Parent or kernel can register and unban.
    if (!isSelf && !isParent && !isKernel) {
        return; // Blocked: not self, not parent, not kernel.
    }

    bool found = false;
    for (usz i = 0; i < _state->instructionHooks.size(); ++i) {
        if (_state->instructionHooks[i].name == instruction) {
            _state->instructionHooks[i].callback = callback;
            // Self CANNOT unban — only parent/kernel can change banned state.
            if (!isSelf) {
                _state->instructionHooks[i].banned = false;
            }
            // If self is calling on a banned instruction, callback is registered
            // but the instruction stays banned. The callback fires (if AOT allows)
            // but the instruction itself remains replaced by ud2.
            found = true;
            break;
        }
    }
    if (!found) {
        TaskState::InstructionHook hook;
        hook.name = instruction;
        hook.callback = callback;
        hook.banned = false; // New hooks start unbanned.
        _state->instructionHooks.push(hook);
    }
    AOT::destroyCache(_state->aotCache);
}

void Task::offInstruction(const String& instruction) {
    if (!_state) return;
    Task caller = Task::current();
    // offInstruction is FINAL: only parent or kernel can ban.
    // Self CANNOT call offInstruction — that would be an escape vector.
    if (caller.valid() && caller.id() != 0) {
        if (_state->parentId != caller.id()) {
            return; // Blocked: only parent can ban instructions.
        }
    }
    bool found = false;
    for (usz i = 0; i < _state->instructionHooks.size(); ++i) {
        if (_state->instructionHooks[i].name == instruction) {
            _state->instructionHooks[i].callback = Func<void()>();
            _state->instructionHooks[i].banned = true;
            found = true;
            break;
        }
    }
    if (!found) {
        TaskState::InstructionHook hook;
        hook.name = instruction;
        hook.callback = Func<void()>();
        hook.banned = true;
        _state->instructionHooks.push(hook);
    }
    AOT::destroyCache(_state->aotCache);
}

void Task::setMinChildQuota(u64 us) {
    if (!_state) return;
    Task caller = Task::current();
    if (caller.valid() && caller.id() != 0) {
        if (_state->parentId != caller.id()) {
            return; // Blocked: only parent can set min child quota.
        }
    }
    _state->minChildQuotaUs = us;
}

void xi_reset_task_state_for_tests() {
    Task::reset();
}

} // namespace Execution

