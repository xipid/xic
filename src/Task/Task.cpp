/**
 * @file Task.cpp
 * @brief Task lifecycle, IPC, and memory operations — portable C++.
 */

#include "../../include/Task/Task.hpp"



struct GuestMessage {
    unsigned int cmd;
    unsigned int status;
    unsigned long long arg1;
    unsigned long long arg2;
    char payload[256];
};

#include "../../include/Task/Interrupt.hpp"
#include <cstdio>
#include <cstring>
#include <cerrno>
#ifndef _WIN32
#include <sys/mman.h>
#include <unistd.h>
#endif

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
#endif

#if defined(__x86_64__) || defined(_M_X64)
extern "C" void xi_invalidate_sfi_cache();
#else
static inline void xi_invalidate_sfi_cache() {}
#endif

namespace Task {

static void xi_desensitize_on_fetch(TaskState* state, usz dest, usz length);

// -------------------------------------------------------------------------
// Static variables for Task global state
// -------------------------------------------------------------------------

Array<TaskState*> Task::_tasks;
Array<CoreState> Task::_cores;
usz Task::_nextTaskId = 1;
usz Task::_nextTid = 1;
u64 Task::_schedulePeriodUs = 10000;
u32 Task::_interruptIntervalUs = 1000;
Func<void(usz, u32)> Task::_onChangeFrequency;
Array<Task::PhysicalAllocation> Task::_allocations;
Task* Task::instance = nullptr;
Array<Pipe*> TaskState::_allPipes;
usz TaskState::_nextPipeId = 3; // Start at 3 (0=stdin, 1=stdout, 2=stderr reserved)

// -------------------------------------------------------------------------
// Static: current task
// -------------------------------------------------------------------------

static thread_local TaskState* tl_currentTask = nullptr;
static u64 g_host_fs_base = 0;
thread_local usz xi_last_guest_rbx = 0;
thread_local usz xi_last_jit_rip = 0;
thread_local GuestRegs* xi_guest_regs = nullptr;
thread_local usz tl_currently_rewriting_physical = 0;
void xi_assign_tid_if_needed(TaskState* state) {
    if (state && state->tid == 0) {
        state->tid = Task::_nextTid++;
    }
}


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
    if (!state || (!state->isMemoryIsolated && !state->isInstructionIsolated)) return;

    TaskState* prev = xi_get_current_task();
    xi_set_current_task(state);

    for (usz i = 0; i < state->regions.size(); ++i) {
        MemoryRegion& r = state->regions[i];
        if (r.physical && r.executable) {
            AOTRegion* cached = AOT::findCached(state->aotCache, reinterpret_cast<usz>(r.physical), r.size);
             if (!cached) {
                AOTResult res = AOT::rewrite(r.physical, r.size, state->regions, r.base, state);
                if (res.success && res.patchedCode) {
                    AOTRegion reg;
                    reg.originalAddr = reinterpret_cast<usz>(r.physical);
                    reg.originalSize = r.size;
                    reg.patchedCode = res.patchedCode;
                    reg.patchedSize = res.patchedSize;
                    reg.offsetMap = res.offsetMap;
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
    if (!state || (!state->isMemoryIsolated && !state->isInstructionIsolated) || !addr) return addr;
    usz target = reinterpret_cast<usz>(addr);

    for (usz i = 0; i < state->aotCache.size(); ++i) {
        AOTRegion& aot = state->aotCache[i];
        if (aot.patchedCode && target >= aot.originalAddr && target < aot.originalAddr + aot.originalSize) {
            if (aot.offsetMap) {
                usz offset = target - aot.originalAddr;
                if (offset < aot.originalSize && aot.offsetMap[offset] != 0xFFFFFFFF) {
                    return aot.patchedCode + aot.offsetMap[offset];
                }
            }
        }
    }

    Task t(state);
    t.translate(target, 0);

    xi_aot_rewrite_task_regions(state);

    // Re-translate to ensure the target region is loaded if it got evicted during cascading AOT rewrite
    t.translate(target, 0);

    for (usz i = 0; i < state->regions.size(); ++i) {
        MemoryRegion& r = state->regions[i];
        if (r.physical && r.executable) {
            // Check virtual address space
            if (target >= r.base && target < r.base + r.size) {
                AOTRegion* cached = AOT::findCached(state->aotCache, reinterpret_cast<usz>(r.physical), r.size);
                if (cached && cached->patchedCode && cached->offsetMap) {
                    usz offset = target - r.base;
                    if (offset < cached->originalSize && cached->offsetMap[offset] != 0xFFFFFFFF) {
                        return cached->patchedCode + cached->offsetMap[offset];
                    }
                }
            }
            // Check physical address space
            if (target >= reinterpret_cast<usz>(r.physical) &&
                target < reinterpret_cast<usz>(r.physical) + r.size) {
                AOTRegion* cached = AOT::findCached(state->aotCache, reinterpret_cast<usz>(r.physical), r.size);
                if (cached && cached->patchedCode && cached->offsetMap) {
                    usz offset = target - reinterpret_cast<usz>(r.physical);
                    if (offset < cached->originalSize && cached->offsetMap[offset] != 0xFFFFFFFF) {
                        return cached->patchedCode + cached->offsetMap[offset];
                    }
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
    if (state->isMemoryIsolated || state->isInstructionIsolated) {
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
    if (!state->isMemoryIsolated) {
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
#ifndef _WIN32
        ::syscall(158, 0x1002, g_host_fs_base);
#endif
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
#ifndef _WIN32
        ::syscall(158, 0x1002, g_host_fs_base);
#endif
        xi_context_switch(from, &core.idleContext);
        return;
    }
    ::printf("[Context Switch] Switching to task %lu, rip=0x%lx\n", (unsigned long)toState->id, (unsigned long)toState->context.rip);
    ::fflush(stdout);

#ifndef _WIN32
    if (toState->fsBase != 0) {
        ::syscall(158, 0x1002, toState->fsBase);
    } else {
        ::syscall(158, 0x1002, g_host_fs_base);
    }
#endif

    xi_context_switch(from, &toState->context);

#ifndef _WIN32
    ::syscall(158, 0x1002, g_host_fs_base);
#endif
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

    // Inherit instruction configurations
    cs->instructionHooks = _state->instructionHooks;
    cs->instructionTranslators = _state->instructionTranslators;
    cs->bannedList = _state->bannedList;

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
        if (_state->isMemoryIsolated || _state->isInstructionIsolated) {
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
        if (_state->isMemoryIsolated || _state->isInstructionIsolated) {
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
            xi_assign_tid_if_needed(_state);
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
                        xi_assign_tid_if_needed(next);
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

bool Task::isMapped(usz base, usz size) const {
    if (!_state) return false;
    for (usz i = 0; i < _state->regions.size(); ++i) {
        const MemoryRegion& r = _state->regions[i];
        if (base < r.base + r.size && base + size > r.base) {
            return true;
        }
    }
    return false;
}

void Task::translate(const MemoryTranslation& mt) {
    if (!_state) return;
    _state->isMemoryIsolated = true;
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
    _state->isMemoryIsolated = true;
    Task caller = Task::current();
    if (caller.valid() && caller.id() != 0) {
        if (_state->parentId != caller.id()) {
            return; // Blocked: only parent can copy memory.
        }
    }

    u8* srcPhys = nullptr;
    MemoryRegion* srcRegion = nullptr;

    // 1. Try to find source in child's own regions (for intra-task copies)
    for (usz i = 0; i < _state->regions.size(); ++i) {
        MemoryRegion& r = _state->regions[i];
        if (source >= r.base && source < r.base + r.size) {
            srcPhys = r.physical + (source - r.base);
            srcRegion = &r;
            break;
        }
    }

    // If dest is within the same region as source, perform immediate copy and split if CoW.
    if (srcRegion && dest >= srcRegion->base && dest < srcRegion->base + srcRegion->size) {
        if (dest + length <= srcRegion->base + srcRegion->size) {
            if (srcRegion->cow) {
                u8* oldPhys = srcRegion->physical;
                usz sz = srcRegion->size;
                u8* newPhys = new u8[sz];
                std::memcpy(newPhys, oldPhys, sz);
                Task::registerAllocation(newPhys, sz, false);
                srcRegion->physical = newPhys;
                srcRegion->writable = true;
                srcRegion->cow = false;
                Task::releaseAllocation(oldPhys);
                xi_invalidate_sfi_cache();
                srcPhys = srcRegion->physical + (source - srcRegion->base);
            }
            std::memmove(srcRegion->physical + (dest - srcRegion->base), srcPhys, length);
            return;
        }
    }

    // 2. If not found in child, find source physical address in parent/caller space
    if (!srcPhys) {
        if (caller.valid() && caller._state) {
            for (usz i = 0; i < caller._state->regions.size(); ++i) {
                MemoryRegion& r = caller._state->regions[i];
                if (source >= r.base && source < r.base + r.size) {
                    srcPhys = r.physical + (source - r.base);
                    break;
                }
            }
            if (!srcPhys && caller.id() == 0) {
                srcPhys = reinterpret_cast<u8*>(source);
            }
        } else {
            srcPhys = reinterpret_cast<u8*>(source);
        }
    }

    if (srcPhys) {
        // Unmap the destination range first
        unmap(dest, length);

        // Map the source physical memory into dest as read-only, cow = true
        MemoryRegion region;
        region.base = dest;
        region.size = length;
        region.physical = srcPhys;
        region.writable = false;
        region.cow = true;
        region.owned = true;

        Task::retainAllocation(srcPhys);
        _state->regions.push(region);

        // Also mark the source region as CoW too!
        if (caller.valid() && caller._state) {
            for (usz i = 0; i < caller._state->regions.size(); ++i) {
                MemoryRegion& r = caller._state->regions[i];
                if (srcPhys >= r.physical && srcPhys < r.physical + r.size) {
                    r.writable = false;
                    r.cow = true;
                    break;
                }
            }
        }

        xi_invalidate_sfi_cache();
    }
}

void Task::physicalCopy(usz source, usz dest, usz length) {
    if (!_state || length == 0) return;
    _state->isMemoryIsolated = true;
    Task caller = Task::current();
    if (caller.valid() && caller.id() != 0) {
        if (_state->parentId != caller.id()) {
            return; // Blocked: only parent can copy memory.
        }
    }

    u8* srcPhys = nullptr;

    // 1. Try to find source in child's own regions
    for (usz i = 0; i < _state->regions.size(); ++i) {
        MemoryRegion& r = _state->regions[i];
        if (source >= r.base && source < r.base + r.size) {
            srcPhys = r.physical + (source - r.base);
            break;
        }
    }

    // 2. If not found in child, find source physical address in parent/caller space
    if (!srcPhys) {
        if (caller.valid() && caller._state) {
            for (usz i = 0; i < caller._state->regions.size(); ++i) {
                MemoryRegion& r = caller._state->regions[i];
                if (source >= r.base && source < r.base + r.size) {
                    srcPhys = r.physical + (source - r.base);
                    break;
                }
            }
            if (!srcPhys && caller.id() == 0) {
                srcPhys = reinterpret_cast<u8*>(source);
            }
        } else {
            srcPhys = reinterpret_cast<u8*>(source);
        }
    }

    // Find dest physical address in child task space
    u8* dstPhys = nullptr;
    for (usz i = 0; i < _state->regions.size(); ++i) {
        MemoryRegion& r = _state->regions[i];
        if (dest >= r.base && dest < r.base + r.size) {
            if (r.cow) {
                u8* oldPhys = r.physical;
                usz sz = r.size;
                u8* newPhys = new u8[sz];
                std::memcpy(newPhys, oldPhys, sz);
                Task::registerAllocation(newPhys, sz, false);
                r.physical = newPhys;
                r.writable = true;
                r.cow = false;
                Task::releaseAllocation(oldPhys);
                xi_invalidate_sfi_cache();
            }
            dstPhys = r.physical + (dest - r.base);
            break;
        }
    }

    if (srcPhys && dstPhys) {
        std::memcpy(dstPhys, srcPhys, length);
    }
}

static void xi_ensure_memory_limits(TaskState* state, usz dest, usz length) {
    if (!state) return;
    Task self(state);
    Task parent = self.parent();
    if (parent.valid() && parent._state && parent._state->maxChildrenMemory > 0) {
        usz currentTotal = parent.totalChildrenMemory();
        // ::printf("[xi_ensure_memory_limits] dest=0x%lx, length=%lu, currentTotal=%lu, max=%lu\n",
        //          (unsigned long)dest, (unsigned long)length, (unsigned long)currentTotal, (unsigned long)parent._state->maxChildrenMemory);
        // ::fflush(stdout);
        while (currentTotal + length > parent._state->maxChildrenMemory) {
            bool swappedAny = false;
            for (usz c = 0; c < parent._state->childIds.size(); ++c) {
                Task child = Task::findTask(parent._state->childIds[c]);
                if (!child.valid()) continue;
                if ((child._state->swapCallback || child._state->swapRanges.size() > 0) && child._state->regions.size() > 0) {
                    long long candidateIdx = -1;
                    u64 minAccessTicks = (u64)-1;
                    int bestPriority = 2; // 0 = prioritized, 1 = deprioritized

                    for (usz r = 0; r < child._state->regions.size(); ++r) {
                        const MemoryRegion& reg = child._state->regions[r];
                        if (reinterpret_cast<usz>(reg.physical) == tl_currently_rewriting_physical) {
                            continue;
                        }
                        if (dest >= reg.base && dest < reg.base + reg.size) {
                            continue;
                        }

                        // Protect active JIT code region
                        bool isActiveCode = false;
                        if (xi_last_jit_rip != 0) {
                            AOTRegion* cached = AOT::findCached(child._state->aotCache, reinterpret_cast<usz>(reg.physical), reg.size);
                            if (cached && cached->patchedCode) {
                                if (xi_last_jit_rip >= reinterpret_cast<usz>(cached->patchedCode) &&
                                    xi_last_jit_rip < reinterpret_cast<usz>(cached->patchedCode) + cached->patchedSize) {
                                    isActiveCode = true;
                                }
                            }
                        }
                        if (isActiveCode) {
                            continue;
                        }

                        // Determine priority: check if it overlaps any registered SwapRange
                        int priority = 1; // Default deprioritized (unlisted)
                        bool hasHandler = child._state->swapCallback.isValid();

                        for (usz s = 0; s < child._state->swapRanges.size(); ++s) {
                            const TaskState::SwapRange& sr = child._state->swapRanges[s];
                            if (reg.base < sr.end && reg.base + reg.size > sr.start) {
                                priority = 0; // Prioritized (listed)
                                hasHandler = true;
                                break;
                            }
                        }

                        if (!hasHandler) {
                            continue; // No swap handler available for this region, cannot evict
                        }

                        if (priority < bestPriority) {
                            bestPriority = priority;
                            minAccessTicks = reg.lastAccessTicks;
                            candidateIdx = (long long)r;
                        } else if (priority == bestPriority) {
                            if (reg.lastAccessTicks < minAccessTicks) {
                                minAccessTicks = reg.lastAccessTicks;
                                candidateIdx = (long long)r;
                            }
                        }
                    }

                    if (candidateIdx != -1) {
                        MemoryRegion regionToEvict = child._state->regions[(usz)candidateIdx];
                        Func<void(usz, usz)> cb = child._state->swapCallback;
                        for (usz s = 0; s < child._state->swapRanges.size(); ++s) {
                            const TaskState::SwapRange& sr = child._state->swapRanges[s];
                            if (regionToEvict.base < sr.end && regionToEvict.base + regionToEvict.size > sr.start) {
                                cb = sr.callback;
                                break;
                            }
                        }

                        // Put the triggering task on a temporary sleep
                        self.stop(1);

                        TaskState* prev = xi_get_current_task();
                        xi_set_current_task(nullptr);
                        if (cb) {
                            cb(regionToEvict.base, regionToEvict.size);
                        }
                        child.unmap(regionToEvict.base, regionToEvict.size);
                        xi_set_current_task(prev);
                        swappedAny = true;
                        break;
                    }
                }
            }
            if (!swappedAny) break;
            currentTotal = parent.totalChildrenMemory();
        }
    }
}

void Task::alloc(usz dest, usz length) {
    if (!_state || length == 0) return;
    _state->isMemoryIsolated = true;
    xi_ensure_memory_limits(_state, dest, length);
    Task caller = Task::current();
    if (caller.valid() && caller.id() != 0) {
        if (_state->parentId != caller.id() && _state->id != caller.id()) {
            return; // Blocked: only parent or self can allocate memory.
        }
    }

    u8* mem = nullptr;
#ifndef _WIN32
    if (_state->isMemoryIsolated && dest >= 65536) {
        usz mapAddr = dest & ~4095;
        usz mapLen = (length + 4095) & ~4095;
        void* mapped = ::mmap(reinterpret_cast<void*>(mapAddr), mapLen, PROT_READ | PROT_WRITE | PROT_EXEC, MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, -1, 0);
        if (mapped != MAP_FAILED) {
            mem = static_cast<u8*>(mapped) + (dest - mapAddr);
            Task::registerAllocation(mem, length, true);
        } else {
            ::printf("[Task::alloc] mmap FIXED failed: dest=0x%lx, mapAddr=0x%lx, length=%lu, errno=%d\n",
                     (unsigned long)dest, (unsigned long)mapAddr, (unsigned long)length, errno);
            ::fflush(stdout);
        }
    }
#endif

    if (!mem) {
        mem = xi_allocate_and_carve(_state, length, false);
    }
    if (!mem) return;

    MemoryRegion region;
    region.base = dest;
    region.size = length;
    region.physical = mem;
    region.writable = true;
    region.executable = true; // All memory is writable and executable
    region.owned = true;

    _state->regions.push(region);
    xi_invalidate_sfi_cache();
    xi_desensitize_on_fetch(_state, dest, length);
}



static void xi_desensitize_on_fetch(TaskState* state, usz dest, usz length) {
    if (!state || length == 0) return;
    usz mapStart = dest;
    usz mapEnd = dest + length;

    Array<TaskState::FetchRange> newRanges;
    for (usz i = 0; i < state->fetchRanges.size(); ++i) {
        const TaskState::FetchRange& fr = state->fetchRanges[i];
        if (mapEnd <= fr.start || mapStart >= fr.end) {
            newRanges.push(fr);
        } else {
            if (fr.start < mapStart) {
                TaskState::FetchRange left = fr;
                left.end = mapStart;
                newRanges.push(left);
            }
            if (fr.end > mapEnd) {
                TaskState::FetchRange right = fr;
                right.start = mapEnd;
                newRanges.push(right);
            }
        }
    }
    state->fetchRanges = Xi::Move(newRanges);
}

void Task::map(usz source, usz dest, usz length) {
    if (!_state || length == 0) return;
    _state->isMemoryIsolated = true;
    xi_ensure_memory_limits(_state, dest, length);

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
    xi_invalidate_sfi_cache();
    xi_desensitize_on_fetch(_state, dest, length);
}

void Task::copyAndMap(usz source, usz dest, usz length) {
    if (!_state || length == 0) return;

    Task caller = Task::current();
    if (caller.valid() && _state->id == caller.id()) {
        return;
    }
    if (caller.valid() && caller.id() != 0) {
        if (_state->parentId != caller.id()) {
            return;
        }
    }

    u8* srcPhys = nullptr;
    usz srcOff = 0;

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

    if (!srcPhys) return;

    static constexpr usz kMaxCopyMapCacheSize = 8;
    if (_state->copyMapCache.size() >= kMaxCopyMapCacheSize) {
        TaskState::CopyMapRegion evicted = _state->copyMapCache[0];
        unmap(evicted.dest, evicted.size);
        for (usz j = 0; j + 1 < _state->copyMapCache.size(); ++j) {
            _state->copyMapCache[j] = _state->copyMapCache[j + 1];
        }
        _state->copyMapCache.pop();
    }

    u8* copyPhys = new u8[length];
    Task::registerAllocation(copyPhys, length);
    std::memcpy(copyPhys, srcPhys + srcOff, length);

    MemoryRegion region;
    region.base = dest;
    region.size = length;
    region.physical = copyPhys;
    region.writable = true;
    region.executable = true;
    region.owned = true;

    _state->regions.push(region);
    xi_invalidate_sfi_cache();

    TaskState::CopyMapRegion cmr;
    cmr.dest = dest;
    cmr.size = length;
    _state->copyMapCache.push(cmr);

    xi_desensitize_on_fetch(_state, dest, length);
}

usz Task::translate(usz destAddr, usz length) {
    if (!_state) return 0;

    bool foundOverlap = true;
    while (foundOverlap) {
        foundOverlap = false;
        usz queryEnd = (length == 0) ? destAddr + 1 : destAddr + length;
        for (usz i = 0; i < _state->fetchRanges.size(); ++i) {
            const TaskState::FetchRange& fr = _state->fetchRanges[i];
            if (queryEnd > fr.start && destAddr < fr.end) {
                if (fr.callback) {
                    Func<void(usz, usz)> cb = fr.callback;
                    TaskState* prev = xi_get_current_task();
                    xi_set_current_task(nullptr);
                    cb(destAddr, queryEnd);
                    xi_set_current_task(prev);
                }
                foundOverlap = true;
                break;
            }
        }
    }

    for (usz i = 0; i < _state->regions.size(); ++i) {
        const MemoryRegion& r = _state->regions[i];
        if (r.physical) {
            if (destAddr >= r.base && destAddr < r.base + r.size) {
                usz offset = destAddr - r.base;
                if (length > 0) {
                    if (offset + length > r.size) {
                        return 0;
                    }
                }
                return reinterpret_cast<usz>(r.physical + offset);
            }
        }
    }

    return 0;
}

void Task::unmap(usz dest, usz length) {
    if (!_state) return;
    _state->isMemoryIsolated = true;
    Task caller = Task::current();
    std::printf("[Task::unmap] dest=0x%lx, length=0x%lx, caller.id=%d, caller.valid=%d, parentId=%d\n",
                (long)dest, (long)length, (int)(caller.valid() ? caller.id() : -1), (int)caller.valid(), (int)_state->parentId);
    if (caller.valid() && caller.id() != 0) {
        if (_state->parentId != caller.id()) {
            std::printf("[Task::unmap] BLOCKED! caller is not parent\n");
            return; // Blocked: only parent can unmap memory.
        }
    }

    for (long long i = (long long)_state->regions.size() - 1; i >= 0; --i) {
        MemoryRegion& r = _state->regions[(usz)i];
        // Check overlap.
        if (r.base < dest + length && r.base + r.size > dest) {
            std::printf("[Task::unmap] Removing region base=0x%lx, size=0x%lx\n", (long)r.base, (long)r.size);
            if (r.physical) {
                // Do not invalidate AOT cache on unmap/eviction so return addresses remain valid.
                // AOT::invalidate(_state->aotCache, reinterpret_cast<usz>(r.physical), r.size);
                Task::releaseAllocation(r.physical);
            }
            // Remove by shifting elements left to preserve chronological order (FIFO)
            for (usz j = (usz)i; j + 1 < _state->regions.size(); ++j) {
                _state->regions[j] = _state->regions[j + 1];
            }
            _state->regions.pop();
        }
    }

    // Re-sensitize fetchRanges for the unmapped area from originalFetchRanges
    for (usz i = 0; i < _state->originalFetchRanges.size(); ++i) {
        const TaskState::FetchRange& ofr = _state->originalFetchRanges[i];
        usz intersectStart = dest > ofr.start ? dest : ofr.start;
        usz intersectEnd = (dest + length) < ofr.end ? (dest + length) : ofr.end;
        if (intersectStart < intersectEnd) {
            TaskState::FetchRange newFr = ofr;
            newFr.start = intersectStart;
            newFr.end = intersectEnd;
            _state->fetchRanges.push(newFr);
        }
    }
    xi_invalidate_sfi_cache();
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

    // Activate memory isolation
    _state->isMemoryIsolated = true;
    xi_invalidate_sfi_cache();
}

#if defined(__x86_64__) || defined(_M_X64)
extern "C" __attribute__((noinline)) void xi_context_save(TaskContext* ctx) {
    __asm__ volatile(
        "mov %%rbx, 24(%0)\n\t"
        "mov %%r12, 32(%0)\n\t"
        "mov %%r13, 40(%0)\n\t"
        "mov %%r14, 48(%0)\n\t"
        "mov %%r15, 56(%0)\n\t"
        "mov %%rbp, 16(%0)\n\t"
        "leaq 8(%%rsp), %%rax\n\t"
        "mov %%rax, 8(%0)\n\t"
        "mov (%%rsp), %%rax\n\t"
        "mov %%rax, 0(%0)\n\t"
        :
        : "r"(ctx)
        : "rax", "memory"
    );
}
#endif

static usz sys_mmap(usz addr, usz length, int prot, int flags, int fd, usz offset, TaskState* state) {
    if (length == 0) return (usz)-1;
    usz dest = addr;
    if (dest == 0) {
        if (state->mmapAddr == 0) {
            state->mmapAddr = 0x40000000;
        }
        dest = state->mmapAddr;
        state->mmapAddr += (length + 4095) & ~4095;
    }
    dest = dest & ~4095;
    length = (length + 4095) & ~4095;
    
    Task t(state);
    t.unmap(dest, length);
    t.alloc(dest, length);
    
    bool writable = (prot & 2) != 0;
    bool executable = (prot & 4) != 0;
    for (usz i = 0; i < state->regions.size(); ++i) {
        MemoryRegion& r = state->regions[i];
        if (r.base == dest && r.size == length) {
            r.writable = writable;
            r.executable = executable;
            break;
        }
    }
    return dest;
}

static usz sys_mprotect(usz addr, usz len, int prot, TaskState* state) {
    bool writable = (prot & 2) != 0;
    bool executable = (prot & 4) != 0;
    
    usz start = addr;
    usz end = addr + len;
    
    for (usz i = 0; i < state->regions.size(); ++i) {
        MemoryRegion& r = state->regions[i];
        if (r.base < end && r.base + r.size > start) {
            if (start > r.base) {
                MemoryRegion left = r;
                left.size = start - r.base;
                Task::retainAllocation(r.physical);
                
                r.physical += (start - r.base);
                r.size -= (start - r.base);
                r.base = start;
                
                state->regions.push(left);
                i = 0;
                continue;
            }
            if (end < r.base + r.size) {
                MemoryRegion right = r;
                right.base = end;
                right.size = (r.base + r.size) - end;
                right.physical += (end - r.base);
                Task::retainAllocation(r.physical);
                
                r.size = end - r.base;
                
                state->regions.push(right);
                i = 0;
                continue;
            }
            r.writable = writable;
            r.executable = executable;
        }
    }
    
    xi_invalidate_sfi_cache();
    return 0;
}

static usz sys_fork(TaskState* state) {
    if (!xi_guest_regs) return (usz)-1;

    Task parent(state);
    Task child = parent.spawn();
    if (!child.valid()) {
        return (usz)-1;
    }
    usz child_id = child.id();

    // Copy memory regions with CoW
    for (usz i = 0; i < state->regions.size(); ++i) {
        MemoryRegion& r = state->regions[i];
        MemoryRegion childRegion = r;
        childRegion.writable = false;
        childRegion.cow = true;
        childRegion.owned = true;
        Task::retainAllocation(r.physical);
        child._state->regions.push(childRegion);
        
        r.writable = false;
        r.cow = true;
    }

    xi_invalidate_sfi_cache();

    // Copy stack
    child._state->stackSize = state->stackSize;
    child._state->stack = new u8[state->stackSize];
    child._state->stackOwned = true;
    Task::registerAllocation(child._state->stack, child._state->stackSize, false);
    std::memcpy(child._state->stack, state->stack, state->stackSize);

    // Save parent context first
#if defined(__x86_64__) || defined(_M_X64)
    xi_context_save(&state->context);
#endif

    TaskState* cur = xi_get_current_task();
    if (cur && cur->id == child_id) {
        // Child execution path
        usz regsOffset = reinterpret_cast<usz>(xi_guest_regs) - reinterpret_cast<usz>(state->stack);
        xi_guest_regs = reinterpret_cast<GuestRegs*>(cur->stack + regsOffset);
        if (xi_guest_regs) {
            xi_guest_regs->rax = 0;
        }
        return 0;
    }

    // Parent execution path
    child._state->context = state->context;
    usz stackOffset = reinterpret_cast<usz>(child._state->stack) - reinterpret_cast<usz>(state->stack);
    child._state->context.rsp += stackOffset;
    child._state->context.rbp += stackOffset;

    usz regsOffset = reinterpret_cast<usz>(xi_guest_regs) - reinterpret_cast<usz>(state->stack);
    GuestRegs* child_regs = reinterpret_cast<GuestRegs*>(child._state->stack + regsOffset);
    child_regs->rax = 0;

    child._state->status = TaskStatus::Ready;
    Task::enqueue(child_id);

    return child_id;
}

static usz sys_clone(usz flags, usz child_stack, TaskState* state) {
    if (!xi_guest_regs) return (usz)-1;

    if (child_stack == 0) {
        return sys_fork(state);
    }

    Task parent(state);
    Task child = parent.spawn();
    if (!child.valid()) return (usz)-1;
    usz child_id = child.id();

    // Copy memory regions with CoW
    for (usz i = 0; i < state->regions.size(); ++i) {
        MemoryRegion& r = state->regions[i];
        MemoryRegion childRegion = r;
        childRegion.writable = false;
        childRegion.cow = true;
        childRegion.owned = true;
        Task::retainAllocation(r.physical);
        child._state->regions.push(childRegion);
        r.writable = false;
        r.cow = true;
    }

    xi_invalidate_sfi_cache();

    // Copy stack
    child._state->stackSize = state->stackSize;
    child._state->stack = new u8[state->stackSize];
    child._state->stackOwned = true;
    Task::registerAllocation(child._state->stack, child._state->stackSize, false);
    std::memcpy(child._state->stack, state->stack, state->stackSize);

    // Save parent context
#if defined(__x86_64__) || defined(_M_X64)
    xi_context_save(&state->context);
#endif

    TaskState* cur = xi_get_current_task();
    if (cur && cur->id == child_id) {
        usz child_stack_phys = child.translate(child_stack);
        if (child_stack_phys) {
            xi_guest_regs = reinterpret_cast<GuestRegs*>(child_stack_phys - sizeof(GuestRegs));
        } else {
            usz regsOffset = reinterpret_cast<usz>(xi_guest_regs) - reinterpret_cast<usz>(state->stack);
            xi_guest_regs = reinterpret_cast<GuestRegs*>(cur->stack + regsOffset);
        }
        if (xi_guest_regs) {
            xi_guest_regs->rax = 0;
        }
        return 0;
    }

    // Parent path
    child._state->context = state->context;
    usz child_stack_phys = child.translate(child_stack);
    if (child_stack_phys) {
        child_stack_phys -= sizeof(GuestRegs);
        std::memcpy(reinterpret_cast<void*>(child_stack_phys), xi_guest_regs, sizeof(GuestRegs));
        GuestRegs* child_regs = reinterpret_cast<GuestRegs*>(child_stack_phys);
        child_regs->rax = 0;
        child._state->context.rsp = child_stack_phys;
    } else {
        usz stackOffset = reinterpret_cast<usz>(child._state->stack) - reinterpret_cast<usz>(state->stack);
        child._state->context.rsp += stackOffset;
        child._state->context.rbp += stackOffset;
        usz regsOffset = reinterpret_cast<usz>(xi_guest_regs) - reinterpret_cast<usz>(state->stack);
        GuestRegs* child_regs = reinterpret_cast<GuestRegs*>(child._state->stack + regsOffset);
        child_regs->rax = 0;
    }

    child._state->status = TaskStatus::Ready;
    Task::enqueue(child_id);

    return child_id;
}

static void sys_exit(int code, TaskState* state) {
    state->status = TaskStatus::Finished;
    if (state->returnValue) {
        delete[] static_cast<u8*>(state->returnValue);
    }
    state->returnValueSize = sizeof(int);
    state->returnValue = new u8[sizeof(int)];
    std::memcpy(state->returnValue, &code, sizeof(int));
    Task::yield();
}

static usz translate_guest_addr(TaskState* state, usz addr, usz length) {
    if (!state) return 0;
    if (state->stack) {
        usz stack_start = reinterpret_cast<usz>(state->stack);
        usz stack_end = stack_start + state->stackSize;
        if (addr >= stack_start && addr < stack_end) {
            if (addr + length >= addr && addr + length <= stack_end) {
                return addr;
            }
        }
    }
    Task t(state);
    usz phys = t.translate(addr, length);
    if (!phys && !state->isMemoryIsolated) {
        phys = addr;
    }
    return phys;
}

extern "C" void xi_emulate_syscall(TaskState* state, GuestRegs* regs) {
    if (!state || !regs) return;

    u64 rax = regs->rax;

    // 1. Check if there is a custom hook or sub-handler
    TaskState* curr = state;
    Func<void()> cb;
    bool hasHook = false;
    while (curr) {
        std::printf("[xi_emulate_syscall] curr->id=%lu, hooks.size=%lu\n", (unsigned long)curr->id, (unsigned long)curr->instructionHooks.size());
        std::fflush(stdout);
        String subName = "syscall:";
        subName += String(rax);
        for (usz i = 0; i < curr->instructionHooks.size(); ++i) {
            const auto& hook = curr->instructionHooks[i];
            std::printf("  hook name=%s, callback.isValid=%d, banned=%d\n", hook.name.c_str(), (int)hook.callback.isValid(), (int)hook.banned);
            std::fflush(stdout);
            if (hook.name == subName) {
                if (hook.callback.isValid()) {
                    cb = hook.callback;
                    hasHook = true;
                }
                break;
            }
        }
        if (hasHook) break;

        for (usz i = 0; i < curr->instructionHooks.size(); ++i) {
            const auto& hook = curr->instructionHooks[i];
            if (hook.name == "syscall") {
                if (hook.callback.isValid()) {
                    cb = hook.callback;
                    hasHook = true;
                }
                break;
            }
        }
        if (hasHook) break;

        if (curr->parentId < Task::_tasks.size() && Task::_tasks[curr->parentId] && curr->id != 0) {
            curr = Task::_tasks[curr->parentId];
        } else {
            curr = nullptr;
        }
    }

    if (hasHook) {
        if (cb.isValid()) {
            GuestRegs* prevRegs = xi_guest_regs;
            xi_guest_regs = regs;
            cb();
            xi_guest_regs = prevRegs;
        }
        return;
    }

    // 1.5. Emulate custom magic guest system calls
    if (rax == 0x78696301) { // get task id
        regs->rax = state->id;
        return;
    }
    if (rax == 0x78696302) { // get parent id
        regs->rax = state->parentId;
        return;
    }
    if (rax == 0x78696304) { // exit task
        sys_exit((int)regs->rdi, state);
        return;
    }
    if (rax == 0x78696303) { // open pipe
        Task t(state);
        regs->rax = t.openPipe();
        return;
    }
    if (rax == 0x78696305) { // log
        usz guest_addr = regs->rdi;
        usz size = regs->rsi;
        Task t(state);
        usz phys = t.translate(guest_addr, size);
        if (phys) {
            u8* buf = new u8[size];
            std::memcpy(buf, reinterpret_cast<void*>(phys), size);
            LogEntry entry;
            entry.ptr = buf;
            entry.size = size;
            state->log.push(entry);
        }
        regs->rax = 0;
        return;
    }

    // 2. Emulate standard Linux system calls natively
    // -- Process identity syscalls --
    if (rax == 39) { // getpid
        regs->rax = state->id;
        return;
    }
    if (rax == 110) { // getppid
        regs->rax = state->parentId;
        return;
    }
    if (rax == 186) { // gettid
        xi_assign_tid_if_needed(state);
        regs->rax = state->tid;
        return;
    }
    if (rax == 102 || rax == 104) { // getuid / getgid -> return task id
        regs->rax = state->id;
        return;
    }
    if (rax == 107 || rax == 108) { // geteuid / getegid -> return task id
        regs->rax = state->id;
        return;
    }
    if (rax == 218) { // set_tid_address -> return tid
        xi_assign_tid_if_needed(state);
        regs->rax = state->tid;
        return;
    }
    if (rax == 158) { // arch_prctl
        int code = (int)regs->rdi;
        usz addr = regs->rsi;
        if (code == 0x1002) { // ARCH_SET_FS
            state->fsBase = addr;
            regs->rax = 0;
            return;
        } else if (code == 0x1001) { // ARCH_SET_GS
            regs->rax = 0;
            return;
        } else if (code == 0x1003) { // ARCH_GET_FS
            Task t(state);
            usz phys = t.translate(addr, sizeof(usz));
            if (phys) {
                *reinterpret_cast<usz*>(phys) = state->fsBase;
                regs->rax = 0;
            } else {
                regs->rax = (usz)-14; // -EFAULT
            }
            return;
        }
        regs->rax = (usz)-22; // -EINVAL
        return;
    }

    // -- Memory syscalls --
    if (rax == 12) { // brk
        usz new_brk = regs->rdi;
        if (state->brkAddr == 0) {
            usz highest = 0x20000000;
            for (usz i = 0; i < state->regions.size(); ++i) {
                usz end = state->regions[i].base + state->regions[i].size;
                if (end > highest) highest = end;
            }
            state->brkAddr = (highest + 4095) & ~4095;
        }
        if (new_brk == 0 || new_brk < state->brkAddr) {
            regs->rax = state->brkAddr;
        } else {
            Task t(state);
            t.alloc(state->brkAddr, new_brk - state->brkAddr);
            state->brkAddr = new_brk;
            regs->rax = new_brk;
        }
        return;
    }
    if (rax == 9) { // mmap
        usz addr = regs->rdi;
        usz length = regs->rsi;
        int prot = (int)regs->rdx;
        int flags = (int)regs->r10;
        int fd = (int)regs->r8;
        usz offset = regs->r9;
        regs->rax = sys_mmap(addr, length, prot, flags, fd, offset, state);
        return;
    }
    if (rax == 11) { // munmap
        usz addr = regs->rdi;
        usz length = regs->rsi;
        Task(state).unmap(addr, length);
        regs->rax = 0;
        return;
    }
    if (rax == 10) { // mprotect
        usz addr = regs->rdi;
        usz length = regs->rsi;
        int prot = (int)regs->rdx;
        regs->rax = sys_mprotect(addr, length, prot, state);
        return;
    }
    if (rax == 28) { // madvise - silently succeed
        regs->rax = 0;
        return;
    }
    if (rax == 25) { // mremap
        usz old_addr = regs->rdi;
        usz old_size = regs->rsi;
        usz new_size = regs->rdx;
        // Simple implementation: alloc new, copy, unmap old
        Task t(state);
        usz new_addr = sys_mmap(0, new_size, 3 /* PROT_READ|PROT_WRITE */, 0x22 /* MAP_PRIVATE|MAP_ANON */, -1, 0, state);
        if (new_addr != (usz)-1) {
            // Copy old data
            usz copy_size = old_size < new_size ? old_size : new_size;
            usz src_phys = t.translate(old_addr, copy_size);
            usz dst_phys = t.translate(new_addr, copy_size);
            if (src_phys && dst_phys) {
                std::memcpy(reinterpret_cast<void*>(dst_phys), reinterpret_cast<void*>(src_phys), copy_size);
            }
            t.unmap(old_addr, old_size);
        }
        regs->rax = new_addr;
        return;
    }

    // -- Process lifecycle syscalls --
    if (rax == 57) { // fork
        regs->rax = sys_fork(state);
        return;
    }
    if (rax == 56) { // clone
        usz flags = regs->rdi;
        usz child_stack = regs->rsi;
        regs->rax = sys_clone(flags, child_stack, state);
        return;
    }
    if (rax == 60 || rax == 231) { // exit / exit_group
        sys_exit((int)regs->rdi, state);
        return;
    }
    if (rax == 61) { // wait4
        // Simple: check if any child is finished
        regs->rax = (usz)-1; // ECHILD by default
        for (usz i = 0; i < state->childIds.size(); ++i) {
            usz cid = state->childIds[i];
            if (cid < Task::_tasks.size() && Task::_tasks[cid]) {
                TaskState* child = Task::_tasks[cid];
                if (child->status == TaskStatus::Finished || child->status == TaskStatus::Destroyed) {
                    regs->rax = cid;
                    break;
                }
            }
        }
        return;
    }

    // -- Pipe / File descriptor syscalls --
    if (rax == 22) { // pipe (int pipefd[2])
        Task t(state);
        usz pipeId = t.openPipe();
        usz pipefd_addr = regs->rdi;
        usz phys = translate_guest_addr(state, pipefd_addr, 8);
        if (phys) {
            reinterpret_cast<int*>(phys)[0] = (int)pipeId; // read end
            reinterpret_cast<int*>(phys)[1] = (int)pipeId; // write end (same pipe, bidirectional)
        }
        regs->rax = 0;
        return;
    }
    if (rax == 293) { // pipe2 (int pipefd[2], int flags)
        Task t(state);
        usz pipeId = t.openPipe();
        int sysFlags = (int)regs->rsi;
        Pipe* pipe = t.getPipe(pipeId);
        if (pipe && (sysFlags & 0x800)) { // O_NONBLOCK
            pipe->setBlocking(false);
        }
        if (pipe && (sysFlags & 0x80000)) { // O_CLOEXEC
            pipe->flags |= 0x80000;
        }
        usz pipefd_addr = regs->rdi;
        usz phys = translate_guest_addr(state, pipefd_addr, 8);
        if (phys) {
            reinterpret_cast<int*>(phys)[0] = (int)pipeId;
            reinterpret_cast<int*>(phys)[1] = (int)pipeId;
        }
        regs->rax = 0;
        return;
    }
    if (rax == 0) { // read(fd, buf, count)
        int fd = (int)regs->rdi;
        usz buf_addr = regs->rsi;
        usz count = regs->rdx;
        Task t(state);
        Pipe* pipe = t.getPipe((usz)fd);
        if (pipe) {
            usz buf_phys = translate_guest_addr(state, buf_addr, count);
            if (buf_phys) {
                regs->rax = t.read(pipe, reinterpret_cast<void*>(buf_phys), count);
            } else {
                regs->rax = (u64)-14; // -EFAULT
            }
            return;
        }
        if (fd == 0 || fd == 1 || fd == 2) {
            usz buf_phys = translate_guest_addr(state, buf_addr, count);
            if (buf_phys) {
                regs->rax = (u64)::read(fd, reinterpret_cast<void*>(buf_phys), count);
            } else {
                regs->rax = (u64)-14; // -EFAULT
            }
            return;
        }
        // Fall through to native syscall for real fds
    }
    if (rax == 1) { // write(fd, buf, count)
        int fd = (int)regs->rdi;
        usz buf_addr = regs->rsi;
        usz count = regs->rdx;
        Task t(state);
        Pipe* pipe = t.getPipe((usz)fd);
        if (pipe && !pipe->closed) {
            usz buf_phys = translate_guest_addr(state, buf_addr, count);
            if (buf_phys) {
                regs->rax = t.write(pipe, reinterpret_cast<void*>(buf_phys), count);
            } else {
                regs->rax = (u64)-14; // -EFAULT
            }
            return;
        }
        if (fd == 0 || fd == 1 || fd == 2) {
            usz buf_phys = translate_guest_addr(state, buf_addr, count);
            if (buf_phys) {
                regs->rax = (u64)::write(fd, reinterpret_cast<void*>(buf_phys), count);
            } else {
                regs->rax = (u64)-14; // -EFAULT
            }
            return;
        }
        // Fall through to native syscall for real fds (stdout, stderr)
    }
    if (rax == 3) { // close(fd)
        int fd = (int)regs->rdi;
        Task t(state);
        Pipe* pipe = t.getPipe((usz)fd);
        if (pipe) {
            t.closePipe((usz)fd);
            regs->rax = 0;
            return;
        }
        // Fall through for real fds
        regs->rax = 0;
        return;
    }
    if (rax == 8) { // lseek(fd, offset, whence)
        int fd = (int)regs->rdi;
        long offset = (long)regs->rsi;
        int whence = (int)regs->rdx;
        Task t(state);
        Pipe* pipe = t.getPipe((usz)fd);
        if (pipe) {
            switch (whence) {
                case 0: // SEEK_SET
                    t.seek(pipe, (usz)offset);
                    regs->rax = (u64)pipe->position;
                    break;
                case 1: // SEEK_CUR
                    t.seek(pipe, pipe->position + (usz)offset);
                    regs->rax = (u64)pipe->position;
                    break;
                case 2: // SEEK_END
                    t.seek(pipe, pipe->writePosition + (usz)offset);
                    regs->rax = (u64)pipe->position;
                    break;
                default:
                    regs->rax = (u64)-22; // -EINVAL
            }
            return;
        }
        // Fall through for real fds
    }
    if (rax == 17) { // pread64(fd, buf, count, offset)
        int fd = (int)regs->rdi;
        usz buf_addr = regs->rsi;
        usz count = regs->rdx;
        usz offset = regs->r10;
        Task t(state);
        Pipe* pipe = t.getPipe((usz)fd);
        if (pipe) {
            usz buf_phys = translate_guest_addr(state, buf_addr, count);
            if (buf_phys) {
                regs->rax = t.pread(pipe, reinterpret_cast<void*>(buf_phys), count, offset);
            } else {
                regs->rax = (u64)-14; // -EFAULT
            }
            return;
        }
    }
    if (rax == 18) { // pwrite64(fd, buf, count, offset)
        int fd = (int)regs->rdi;
        usz buf_addr = regs->rsi;
        usz count = regs->rdx;
        usz offset = regs->r10;
        Task t(state);
        Pipe* pipe = t.getPipe((usz)fd);
        if (pipe) {
            usz buf_phys = translate_guest_addr(state, buf_addr, count);
            if (buf_phys) {
                regs->rax = t.pwrite(pipe, reinterpret_cast<void*>(buf_phys), count, offset);
            } else {
                regs->rax = (u64)-14; // -EFAULT
            }
            return;
        }
    }
    if (rax == 32) { // dup(oldfd)
        Task t(state);
        usz result = t.dup((usz)regs->rdi);
        regs->rax = result ? result : (u64)-9; // -EBADF
        return;
    }
    if (rax == 33) { // dup2(oldfd, newfd)
        Task t(state);
        usz result = t.dup2((usz)regs->rdi, (usz)regs->rsi);
        regs->rax = result ? result : (u64)-9;
        return;
    }
    if (rax == 292) { // dup3(oldfd, newfd, flags)
        Task t(state);
        usz result = t.dup3((usz)regs->rdi, (usz)regs->rsi, (int)regs->rdx);
        regs->rax = result ? result : (u64)-9;
        return;
    }
    if (rax == 72) { // fcntl(fd, cmd, arg)
        int fd = (int)regs->rdi;
        int cmd = (int)regs->rsi;
        long arg = (long)regs->rdx;
        Task t(state);
        Pipe* pipe = t.getPipe((usz)fd);
        if (pipe) {
            regs->rax = (u64)t.fcntl(pipe, cmd, arg);
            return;
        }
        // Fall through for real fds
    }
    if (rax == 5) { // fstat(fd, statbuf)
        int fd = (int)regs->rdi;
        Task t(state);
        Pipe* pipe = t.getPipe((usz)fd);
        if (pipe) {
            // Fill in a minimal stat struct for a pipe
            usz statbuf_addr = regs->rsi;
            usz phys = translate_guest_addr(state, statbuf_addr, 144); // sizeof(struct stat) on x86_64
            if (phys) {
                std::memset(reinterpret_cast<void*>(phys), 0, 144);
                // st_mode = S_IFIFO | 0600
                reinterpret_cast<u32*>(phys + 24)[0] = 0010600;
                // st_size = bytes available
                reinterpret_cast<u64*>(phys + 48)[0] = pipe->writePosition;
                // st_blksize = 4096
                reinterpret_cast<u64*>(phys + 56)[0] = 4096;
            }
            regs->rax = 0;
            return;
        }
    }
    // -- select/poll/epoll syscalls --
    if (rax == 23) { // select(nfds, readfds, writefds, exceptfds, timeout)
        // Simplified: check if any pipe fds in readfds have data
        int nfds = (int)regs->rdi;
        usz readfds_addr = regs->rsi;
        Task t(state);
        int ready = 0;
        if (readfds_addr) {
            usz phys = translate_guest_addr(state, readfds_addr, (usz)((nfds + 7) / 8));
            if (phys) {
                u8* fds = reinterpret_cast<u8*>(phys);
                // Clear the set, then re-set fds that are ready
                u8 resultSet[128] = {};
                for (int fd = 0; fd < nfds && fd < 1024; ++fd) {
                    if (fds[fd / 8] & (1 << (fd % 8))) {
                        Pipe* pipe = t.getPipe((usz)fd);
                        if (pipe && t.poll(pipe)) {
                            resultSet[fd / 8] |= (1 << (fd % 8));
                            ready++;
                        }
                    }
                }
                std::memcpy(fds, resultSet, (usz)((nfds + 7) / 8));
            }
        }
        regs->rax = (u64)ready;
        return;
    }
    if (rax == 7) { // poll(fds, nfds, timeout)
        // struct pollfd { int fd; short events; short revents; }
        usz fds_addr = regs->rdi;
        usz nfds = regs->rsi;
        Task t(state);
        int ready = 0;
        usz phys = translate_guest_addr(state, fds_addr, nfds * 8);
        if (phys) {
            for (usz i = 0; i < nfds; ++i) {
                int* pollfd = reinterpret_cast<int*>(phys + i * 8);
                int fd = pollfd[0];
                short events = reinterpret_cast<short*>(phys + i * 8 + 4)[0];
                short revents = 0;
                Pipe* pipe = t.getPipe((usz)fd);
                if (pipe) {
                    if ((events & 1) && t.poll(pipe)) { // POLLIN
                        revents |= 1;
                    }
                    if ((events & 4) && !pipe->closed) { // POLLOUT
                        revents |= 4;
                    }
                    if (pipe->closed) {
                        revents |= 0x10; // POLLHUP
                    }
                }
                reinterpret_cast<short*>(phys + i * 8 + 6)[0] = revents;
                if (revents) ready++;
            }
        }
        regs->rax = (u64)ready;
        return;
    }
    if (rax == 291) { // epoll_create1(flags)
        // Create a dummy epoll fd (it's just a pipe internally)
        Task t(state);
        usz epfd = t.openPipe();
        regs->rax = epfd;
        return;
    }
    if (rax == 213) { // epoll_create(size) -- deprecated, same as epoll_create1(0)
        Task t(state);
        usz epfd = t.openPipe();
        regs->rax = epfd;
        return;
    }
    if (rax == 233) { // epoll_ctl(epfd, op, fd, event)
        // We accept but largely no-op this - our poll is simpler
        regs->rax = 0;
        return;
    }
    if (rax == 232) { // epoll_wait(epfd, events, maxevents, timeout)
        // Simplified: check all task pipes for data
        Task t(state);
        usz events_addr = regs->rsi;
        int maxevents = (int)regs->rdx;
        int ready = 0;
        // Check all owned pipes for data
        if (state->pipes.size() > 0 && events_addr) {
            usz phys = translate_guest_addr(state, events_addr, (usz)(maxevents * 12)); // struct epoll_event = 12 bytes
            if (phys) {
                for (usz i = 0; i < state->pipes.size() && ready < maxevents; ++i) {
                    Pipe* pipe = state->pipes[i];
                    if (pipe && t.poll(pipe)) {
                        u32* ev = reinterpret_cast<u32*>(phys + (usz)ready * 12);
                        ev[0] = 1; // EPOLLIN
                        ev[1] = (u32)pipe->id;
                        ev[2] = 0;
                        ready++;
                    }
                }
            }
        }
        regs->rax = (u64)ready;
        return;
    }

    // 3. Fallback: run natively on CPU
#if defined(__x86_64__) || defined(_M_X64)
    register u64 r10 asm("r10") = regs->r10;
    register u64 r8  asm("r8")  = regs->r8;
    register u64 r9  asm("r9")  = regs->r9;
    register u64 sys_rax asm("rax") = regs->rax;
    register u64 rdi asm("rdi") = regs->rdi;
    register u64 rsi asm("rsi") = regs->rsi;
    register u64 rdx asm("rdx") = regs->rdx;

    __asm__ volatile(
        "syscall"
        : "+r"(sys_rax)
        : "r"(rdi), "r"(rsi), "r"(rdx), "r"(r10), "r"(r8), "r"(r9)
        : "rcx", "r11", "memory"
    );
    regs->rax = sys_rax;
#endif
}


// -------------------------------------------------------------------------
// IPC
// -------------------------------------------------------------------------

usz translate_address_to_task(Task sourceTask, usz sourceAddr, usz size, Task targetTask) {
    if (!sourceTask.valid() || !targetTask.valid()) return 0;
    
    // 1. Get physical address of sourceAddr in sourceTask's memory space
    usz phys = 0;
    if (sourceTask.id() == 0 || !sourceTask._state->isMemoryIsolated) {
        phys = sourceAddr;
    } else {
        phys = sourceTask.translate(sourceAddr, size);
    }
    if (!phys) return 0;

    // 2. Find if this physical address is already mapped in targetTask
    if (targetTask.id() == 0 || !targetTask._state->isMemoryIsolated) {
        return phys;
    }
    
    for (usz i = 0; i < targetTask._state->regions.size(); ++i) {
        const MemoryRegion& r = targetTask._state->regions[i];
        if (r.physical) {
            usz physStart = reinterpret_cast<usz>(r.physical);
            if (phys >= physStart && phys < physStart + r.size) {
                if (size > 0 && phys + size > physStart + r.size) {
                    continue;
                }
                return r.base + (phys - physStart);
            }
        }
    }
    return 0;
}

void Task::log(void* ptr, usz size) {
    if (!_state) return;
    LogEntry entry;
    entry.ptr = ptr;
    entry.size = size;
    _state->log.push(entry);
}

LogEntry Task::getLogEntry(usz index, Task reader) const {
    if (!_state || index >= _state->log.size()) return LogEntry{nullptr, 0};
    const LogEntry& original = _state->log[index];

    // Check if reader is parent or grandparent of this task
    bool is_ancestor = false;
    usz checkId = _state->id;
    while (true) {
        if (checkId == reader.id()) {
            is_ancestor = true;
            break;
        }
        if (checkId == 0) {
            break;
        }
        Task parent = Task::findTask(checkId);
        if (parent.valid()) {
            checkId = parent._state->parentId;
        } else {
            break;
        }
    }

    void* translated_ptr = nullptr;
    if (is_ancestor) {
        // Case 1: parent/grandparent -> just translation
        usz translated = translate_address_to_task(*this, reinterpret_cast<usz>(original.ptr), original.size, reader);
        if (translated) {
            translated_ptr = reinterpret_cast<void*>(translated);
        } else {
            usz phys = 0;
            if (!_state->isMemoryIsolated) {
                phys = reinterpret_cast<usz>(original.ptr);
            } else {
                phys = translate(reinterpret_cast<usz>(original.ptr), original.size);
                if (!phys) {
                    phys = reinterpret_cast<usz>(original.ptr);
                }
            }
            if (phys) {
                if (!reader._state->isMemoryIsolated) {
                    translated_ptr = reinterpret_cast<void*>(phys);
                } else {
                    for (usz i = 0; i < reader._state->regions.size(); ++i) {
                        const MemoryRegion& r = reader._state->regions[i];
                        if (r.physical) {
                            usz physStart = reinterpret_cast<usz>(r.physical);
                            if (phys >= physStart && phys < physStart + r.size) {
                                translated_ptr = reinterpret_cast<void*>(r.base + (phys - physStart));
                                break;
                            }
                        }
                    }
                }
            }
        }
    } else {
        // Case 2: via sharing (not ancestor) -> CoW mapping in reader
        usz src_phys = 0;
        if (!_state->isMemoryIsolated) {
            src_phys = reinterpret_cast<usz>(original.ptr);
        } else {
            src_phys = translate(reinterpret_cast<usz>(original.ptr), original.size);
            if (!src_phys) {
                src_phys = reinterpret_cast<usz>(original.ptr);
            }
        }

        if (src_phys) {
            if (!reader._state->isMemoryIsolated) {
                u8* buf = new u8[original.size];
                std::memcpy(buf, reinterpret_cast<void*>(src_phys), original.size);
                translated_ptr = buf;
            } else {
                usz dest_vaddr = 0x60000000;
                for (usz i = 0; i < reader._state->regions.size(); ++i) {
                    usz r_end = reader._state->regions[i].base + reader._state->regions[i].size;
                    if (r_end > dest_vaddr) {
                        dest_vaddr = (r_end + 4095) & ~4095;
                    }
                }
                
                reader.unmap(dest_vaddr, original.size);
                MemoryRegion region;
                region.base = dest_vaddr;
                region.size = original.size;
                region.physical = reinterpret_cast<u8*>(src_phys);
                region.writable = false;
                region.cow = true;
                region.owned = true;
                
                Task::retainAllocation(reinterpret_cast<u8*>(src_phys));
                reader._state->regions.push(region);
                
                for (usz i = 0; i < _state->regions.size(); ++i) {
                    MemoryRegion& r = _state->regions[i];
                    if (reinterpret_cast<u8*>(src_phys) >= r.physical && reinterpret_cast<u8*>(src_phys) < r.physical + r.size) {
                        r.writable = false;
                        r.cow = true;
                        break;
                    }
                }
                
                reader._state->isMemoryIsolated = true;
                translated_ptr = reinterpret_cast<void*>(dest_vaddr);
            }
        }
    }

    return LogEntry{translated_ptr, original.size};
}

void* Task::getReturnValue(Task reader) const {
    if (!_state || !_state->returnValue) return nullptr;

    // Check if reader is parent or grandparent of this task
    bool is_ancestor = false;
    usz checkId = _state->id;
    while (true) {
        if (checkId == reader.id()) {
            is_ancestor = true;
            break;
        }
        if (checkId == 0) {
            break;
        }
        Task parent = Task::findTask(checkId);
        if (parent.valid()) {
            checkId = parent._state->parentId;
        } else {
            break;
        }
    }

    void* translated_ptr = nullptr;
    if (is_ancestor) {
        // Case 1: parent/grandparent
        if (!reader._state->isMemoryIsolated) {
            translated_ptr = _state->returnValue;
        } else {
            usz dest_vaddr = 0x60000000;
            for (usz i = 0; i < reader._state->regions.size(); ++i) {
                usz r_end = reader._state->regions[i].base + reader._state->regions[i].size;
                if (r_end > dest_vaddr) {
                    dest_vaddr = (r_end + 4095) & ~4095;
                }
            }
            reader.unmap(dest_vaddr, _state->returnValueSize);
            MemoryRegion region;
            region.base = dest_vaddr;
            region.size = _state->returnValueSize;
            region.physical = static_cast<u8*>(_state->returnValue);
            region.writable = false;
            region.cow = true;
            region.owned = true;
            
            Task::retainAllocation(static_cast<u8*>(_state->returnValue));
            reader._state->regions.push(region);
            reader._state->isMemoryIsolated = true;
            translated_ptr = reinterpret_cast<void*>(dest_vaddr);
        }
    } else {
        // Case 2: via sharing (not ancestor) -> CoW mapping in reader
        if (!reader._state->isMemoryIsolated) {
            u8* buf = new u8[_state->returnValueSize];
            std::memcpy(buf, _state->returnValue, _state->returnValueSize);
            translated_ptr = buf;
        } else {
            usz dest_vaddr = 0x60000000;
            for (usz i = 0; i < reader._state->regions.size(); ++i) {
                usz r_end = reader._state->regions[i].base + reader._state->regions[i].size;
                if (r_end > dest_vaddr) {
                    dest_vaddr = (r_end + 4095) & ~4095;
                }
            }
            reader.unmap(dest_vaddr, _state->returnValueSize);
            MemoryRegion region;
            region.base = dest_vaddr;
            region.size = _state->returnValueSize;
            region.physical = static_cast<u8*>(_state->returnValue);
            region.writable = false;
            region.cow = true;
            region.owned = true;
            
            Task::retainAllocation(static_cast<u8*>(_state->returnValue));
            reader._state->regions.push(region);
            reader._state->isMemoryIsolated = true;
            translated_ptr = reinterpret_cast<void*>(dest_vaddr);
        }
    }
    return translated_ptr;
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

static Pipe* getRealPipe(Pipe* p) {
    while (p && p->source) {
        p = p->source;
    }
    return p;
}

static Pipe* xi_find_pipe(usz pipeId) {
    for (usz i = 0; i < TaskState::_allPipes.size(); ++i) {
        if (TaskState::_allPipes[i] && TaskState::_allPipes[i]->id == pipeId) {
            return TaskState::_allPipes[i];
        }
    }
    return nullptr;
}

static Pipe* xi_first_owned_pipe(TaskState* state) {
    if (!state) return nullptr;
    for (usz i = 0; i < state->ownedPipeIds.size(); ++i) {
        Pipe* p = xi_find_pipe(state->ownedPipeIds[i]);
        if (p && !p->closed) return p;
    }
    return nullptr;
}

static void xi_auto_share_on_send(TaskState* sender, TaskState* receiver) {
    if (!sender || !receiver) return;
    usz senderId = sender->id;
    bool alreadyShared = false;
    for (usz i = 0; i < receiver->sharedIds.size(); ++i) {
        if (receiver->sharedIds[i] == senderId) {
            alreadyShared = true;
            break;
        }
    }
    if (!alreadyShared) {
        receiver->sharedIds.push(senderId);
    }
}

static bool xi_can_send_to(TaskState* sender, TaskState* receiver) {
    if (!sender || !receiver) return false;
    // Can send to: self, parent, children, shared tasks
    if (sender->id == receiver->id) return true;
    if (sender->parentId == receiver->id) return true; // parent
    if (receiver->parentId == sender->id) return true; // child
    if (sender->parentId == receiver->parentId) return true; // sibling
    for (usz i = 0; i < sender->sharedIds.size(); ++i) {
        if (sender->sharedIds[i] == receiver->id) return true;
    }
    for (usz i = 0; i < receiver->sharedIds.size(); ++i) {
        if (receiver->sharedIds[i] == sender->id) return true;
    }
    return false;
}

static void xi_write_pipe_entry(TaskState* senderState, Pipe* pipe, const void* ptr, usz length, usz senderId) {
    pipe = getRealPipe(pipe);
    if (!pipe || pipe->closed || length == 0) return;

    u8* physData = reinterpret_cast<u8*>(const_cast<void*>(ptr));

    if (senderState && senderState->isMemoryIsolated) {
        usz phys = Task(senderState).translate(reinterpret_cast<usz>(ptr), length);
        if (phys) {
            physData = reinterpret_cast<u8*>(phys);
        }
    }

    TaskState* ownerState = nullptr;
    if (pipe->ownerId < Task::_tasks.size()) {
        ownerState = Task::_tasks[pipe->ownerId];
    }

    bool sameTask = (senderState && ownerState && senderState->id == ownerState->id);
    bool isAncestor = false;
    if (senderState && ownerState) {
        usz checkId = ownerState->id;
        while (true) {
            if (checkId == senderState->id) { isAncestor = true; break; }
            if (checkId == 0) break;
            if (checkId < Task::_tasks.size() && Task::_tasks[checkId]) {
                checkId = Task::_tasks[checkId]->parentId;
            } else break;
        }
    }

    PipeEntry entry;
    entry.senderId = senderId;
    entry.size = length;
    entry.position = pipe->writePosition;

    entry.data = physData;
    entry.cow = true;
    entry.isDummy = false;
    Task::retainAllocation(physData);

    pipe->entries.push(entry);
    pipe->writePosition += length;
    if (pipe->writePosition > pipe->logicalSize) {
        pipe->logicalSize = pipe->writePosition;
    }

    if (pipe->onWrite) {
        pipe->onWrite(pipe);
    }
    pipe->checkEviction();

    if (ownerState && ownerState->status == TaskStatus::Paused && ownerState->isWaitingForMessage) {
        ownerState->isWaitingForMessage = false;
        ownerState->status = TaskStatus::Ready;
    }

    if (ownerState && ownerState->status == TaskStatus::Paused && ownerState->isWaitingForMessage) {
        ownerState->isWaitingForMessage = false;
        ownerState->status = TaskStatus::Ready;
    }
}

// Helper: read from pipe at a given byte offset, without modifying pipe->position
static usz xi_pipe_read_at(Pipe* pipe, void* buf, usz length, usz offset) {
    pipe = getRealPipe(pipe);
    if (!pipe || length == 0 || !buf) return 0;

    u8* dst = reinterpret_cast<u8*>(buf);
    usz total_read = 0;

    for (usz i = 0; i < pipe->entries.size() && total_read < length; ++i) {
        PipeEntry& entry = pipe->entries[i];
        usz entryStart = entry.position;
        usz entryEnd = entryStart + entry.size;

        // Skip entries entirely before our read offset
        if (entryEnd <= offset) continue;
        // Stop if entry starts beyond our read range
        if (entryStart >= offset + length) break;

        // Calculate overlap
        usz readStart = (offset > entryStart) ? (offset - entryStart) : 0;
        usz readEnd = ((offset + length - total_read) < entry.size) 
                      ? (offset + length - total_read - entryStart + readStart)
                      : entry.size;
        if (readEnd > entry.size) readEnd = entry.size;
        usz to_copy = readEnd - readStart;
        if (to_copy > length - total_read) to_copy = length - total_read;

        if (entry.isDummy || entry.data == nullptr) {
            std::memset(dst + total_read, 0, to_copy);
        } else {
            std::memcpy(dst + total_read, entry.data + readStart, to_copy);
        }
        total_read += to_copy;
        offset += to_copy;
    }

    return total_read;
}

static void xi_pipe_handle_cache_misses(TaskState* state, Pipe* pipe, usz offset, usz length) {
    if (!pipe->onRead) return;

    usz end = offset + length;
    if (pipe->logicalSize > 0 && end > pipe->logicalSize) {
        end = pipe->logicalSize;
    }
    if (offset >= end) return;

    usz current_pos = offset;
    while (current_pos < end) {
        bool covered = false;
        usz next_entry_start = end;

        for (usz i = 0; i < pipe->entries.size(); ++i) {
            PipeEntry& entry = pipe->entries[i];
            usz entry_start = entry.position;
            usz entry_end = entry_start + entry.size;

            if (entry_start <= current_pos && entry_end > current_pos) {
                covered = true;
                current_pos = entry_end;
                break;
            }
            if (entry_start > current_pos && entry_start < next_entry_start) {
                next_entry_start = entry_start;
            }
        }

        if (!covered) {
            usz gap_start = current_pos;
            usz gap_len = next_entry_start - gap_start;

            pipe->onRead(gap_start, state->id, gap_len);

            current_pos = gap_start + gap_len;
        }
    }
}

void Pipe::dummyWrite(usz pos, usz length) {
    if (length == 0) return;

    usz senderId = 0;
    Task caller = Task::current();
    if (caller.valid()) {
        senderId = caller.id();
    } else {
        senderId = ownerId;
    }

    PipeEntry entry;
    entry.senderId = senderId;
    entry.data = nullptr;
    entry.size = length;
    entry.position = pos;
    entry.cow = false;
    entry.isDummy = true;

    entries.push(entry);

    if (pos + length > writePosition) {
        writePosition = pos + length;
    }
    if (pos + length > logicalSize) {
        logicalSize = pos + length;
    }

    checkEviction();
}

void Pipe::setCaching(usz bytes) {
    maxCachedBytes = bytes;
    checkEviction();
}

void Pipe::checkEviction() {
    if (maxCachedBytes == 0) return;

    usz total_cached = 0;
    for (usz i = 0; i < entries.size(); ++i) {
        if (!entries[i].isDummy && entries[i].data != nullptr) {
            total_cached += entries[i].size;
        }
    }

    usz idx = 0;
    while (total_cached > maxCachedBytes && idx < entries.size()) {
        PipeEntry& entry = entries[idx];
        if (!entry.isDummy && entry.data != nullptr) {
            total_cached -= entry.size;
            if (entry.cow) {
                Task::releaseAllocation(entry.data);
            }
            entries.splice(idx, 1);
        } else {
            idx++;
        }
    }
}

void Pipe::flush() {
    for (usz i = 0; i < entries.size(); ++i) {
        PipeEntry& entry = entries[i];
        if (entry.cow && entry.data != nullptr) {
            Task::releaseAllocation(entry.data);
        }
    }
    entries.clear();
    writePosition = 0;
    position = 0;
    logicalSize = 0;
}

void Pipe::flush(usz pos, usz length) {
    if (length == 0) return;
    usz flush_start = pos;
    usz flush_end = pos + length;

    for (long long i = (long long)entries.size() - 1; i >= 0; --i) {
        PipeEntry& entry = entries[(usz)i];
        usz entry_start = entry.position;
        usz entry_end = entry_start + entry.size;

        if (entry_end <= flush_start || entry_start >= flush_end) {
            continue;
        }

        if (entry_start >= flush_start && entry_end <= flush_end) {
            if (entry.cow && entry.data != nullptr) {
                Task::releaseAllocation(entry.data);
            }
            entries.splice((usz)i, 1);
            continue;
        }

        if (entry_start < flush_start && entry_end > flush_start && entry_end <= flush_end) {
            entry.size = flush_start - entry_start;
            continue;
        }

        if (entry_start >= flush_start && entry_start < flush_end && entry_end > flush_end) {
            usz diff = flush_end - entry_start;
            entry.position = flush_end;
            entry.size -= diff;
            if (entry.data != nullptr) {
                entry.data += diff;
            }
            continue;
        }

        if (entry_start < flush_start && entry_end > flush_end) {
            usz left_size = flush_start - entry_start;
            usz right_size = entry_end - flush_end;
            u8* right_data = entry.data ? (entry.data + (flush_end - entry_start)) : nullptr;

            PipeEntry right_entry;
            right_entry.senderId = entry.senderId;
            right_entry.data = right_data;
            right_entry.size = right_size;
            right_entry.position = flush_end;
            right_entry.cow = entry.cow;
            right_entry.isDummy = entry.isDummy;

            if (right_entry.cow && right_entry.data != nullptr) {
                Task::retainAllocation(right_entry.data);
            }

            entry.size = left_size;

            usz insert_idx = (usz)i + 1;
            entries.push(PipeEntry());
            for (usz k = entries.size() - 1; k > insert_idx; --k) {
                entries[k] = entries[k - 1];
            }
            entries[insert_idx] = right_entry;

            continue;
        }
    }
}

usz Task::openPipe() {
    if (!_state) return 0;

    Pipe* pipe = new Pipe();
    pipe->id = TaskState::_nextPipeId++;
    pipe->ownerId = _state->id;

    TaskState::_allPipes.push(pipe);
    _state->ownedPipeIds.push(pipe->id);
    _state->pipes.push(pipe);

    return pipe->id;
}

void Task::closePipe(usz pipeId) {
    if (!_state) return;

    Pipe* pipe = xi_find_pipe(pipeId);
    if (!pipe) return;
    if (pipe->ownerId != _state->id) return;

    pipe->closed = true;

    for (usz i = 0; i < pipe->entries.size(); ++i) {
        PipeEntry& entry = pipe->entries[i];
        if (entry.cow) {
            Task::releaseAllocation(entry.data);
        }
    }

    for (long long i = (long long)_state->ownedPipeIds.size() - 1; i >= 0; --i) {
        if (_state->ownedPipeIds[(usz)i] == pipeId) {
            usz last = _state->ownedPipeIds.size() - 1;
            if ((usz)i != last) _state->ownedPipeIds[(usz)i] = _state->ownedPipeIds[last];
            _state->ownedPipeIds.pop();
            break;
        }
    }
    for (long long i = (long long)_state->pipes.size() - 1; i >= 0; --i) {
        if (_state->pipes[(usz)i] == pipe) {
            usz last = _state->pipes.size() - 1;
            if ((usz)i != last) _state->pipes[(usz)i] = _state->pipes[last];
            _state->pipes.pop();
            break;
        }
    }
    for (long long i = (long long)TaskState::_allPipes.size() - 1; i >= 0; --i) {
        if (TaskState::_allPipes[(usz)i] == pipe) {
            usz last = TaskState::_allPipes.size() - 1;
            if ((usz)i != last) TaskState::_allPipes[(usz)i] = TaskState::_allPipes[last];
            TaskState::_allPipes.pop();
            break;
        }
    }
    delete pipe;
}

Pipe* Task::getPipe(usz pipeId) {
    if (!_state) return nullptr;
    for (usz i = 0; i < _state->pipes.size(); ++i) {
        if (_state->pipes[i] && _state->pipes[i]->id == pipeId) {
            return _state->pipes[i];
        }
    }
    Pipe* p = xi_find_pipe(pipeId);
    if (p) {
        if (p->ownerId == _state->id) return p;
        if (p->ownerId == _state->parentId) return p;
        for (usz i = 0; i < _state->childIds.size(); ++i) {
            if (p->ownerId == _state->childIds[i]) return p;
        }
        for (usz i = 0; i < _state->sharedIds.size(); ++i) {
            if (p->ownerId == _state->sharedIds[i]) return p;
        }
    }
    return nullptr;
}

usz Task::write(Pipe* pipe, const void* data, usz length) {
    pipe = getRealPipe(pipe);
    if (!_state || !pipe || pipe->closed || length == 0) return 0;
    std::printf("[Host Task::write] pipe->id=%lu, length=%lu, data=%.*s\n",
                (unsigned long)pipe->id, (unsigned long)length, (int)length, (const char*)data);
    std::fflush(stdout);
    usz senderId = 0;
    Task caller = Task::current();
    if (caller.valid()) {
        senderId = caller.id();
    } else {
        senderId = _state->id;
    }
    xi_write_pipe_entry(_state, pipe, data, length, senderId);
    return length;
}

usz Task::read(Pipe* pipe, void* buf, usz length) {
    pipe = getRealPipe(pipe);
    if (!_state || !pipe || length == 0 || !buf) return 0;

    xi_pipe_handle_cache_misses(_state, pipe, pipe->position, length);

    // Check if data is available at current position
    if (pipe->position >= pipe->writePosition) {
        // No data available
        if (!pipe->blocking) {
            // Non-blocking: return 0, caller gets nothing
            return 0;
        }
        // Blocking: return 0 (task should yield and retry)
        return 0;
    }

    usz available = pipe->writePosition - pipe->position;
    usz to_read = (length < available) ? length : available;
    usz bytes_read = xi_pipe_read_at(pipe, buf, to_read, pipe->position);
    pipe->position += bytes_read;
    return bytes_read;
}

usz Task::pread(Pipe* pipe, void* buf, usz length, usz offset) {
    pipe = getRealPipe(pipe);
    if (!_state || !pipe || length == 0 || !buf) return 0;

    xi_pipe_handle_cache_misses(_state, pipe, offset, length);

    return xi_pipe_read_at(pipe, buf, length, offset);
}

usz Task::pwrite(Pipe* pipe, const void* data, usz length, usz offset) {
    pipe = getRealPipe(pipe);
    if (!_state || !pipe || pipe->closed || length == 0) return 0;
    // pwrite: write at offset without changing writePosition
    u8* physData = reinterpret_cast<u8*>(const_cast<void*>(data));
    if (_state->isMemoryIsolated) {
        usz phys = translate(reinterpret_cast<usz>(data), length);
        if (phys) physData = reinterpret_cast<u8*>(phys);
    }
    usz senderId = 0;
    Task caller = Task::current();
    if (caller.valid()) {
        senderId = caller.id();
    } else {
        senderId = _state->id;
    }
    PipeEntry entry;
    entry.senderId = senderId;
    entry.size = length;
    entry.position = offset;
    entry.data = physData;
    entry.cow = true;
    entry.isDummy = false;
    Task::retainAllocation(physData);
    pipe->entries.push(entry);
    // Update writePosition if this extends beyond current end
    if (offset + length > pipe->writePosition) {
        pipe->writePosition = offset + length;
    }
    if (offset + length > pipe->logicalSize) {
        pipe->logicalSize = offset + length;
    }

    if (pipe->onWrite) {
        pipe->onWrite(pipe);
    }
    pipe->checkEviction();

    return length;
}

usz Task::dup(Pipe* pipe, int flags) {
    if (!_state || !pipe) return 0;

    Pipe* newPipe = new Pipe();
    newPipe->id = TaskState::_nextPipeId++;
    newPipe->ownerId = _state->id;
    newPipe->source = pipe;
    newPipe->flags = flags;
    if (flags & 0x80000) { // O_CLOEXEC
        newPipe->flags |= 0x80000;
    }

    TaskState::_allPipes.push(newPipe);
    _state->ownedPipeIds.push(newPipe->id);
    _state->pipes.push(newPipe);

    return newPipe->id;
}

usz Task::dup2(usz oldfd, usz newfd) {
    if (oldfd == newfd) {
        Pipe* oldPipe = getPipe(oldfd);
        if (!oldPipe) return 0;
        return newfd;
    }
    return dup3(oldfd, newfd, 0);
}

usz Task::dup3(usz oldfd, usz newfd, int flags) {
    if (!_state) return 0;
    if (oldfd == newfd) return 0; // invalid argument for dup3
    Pipe* oldPipe = getPipe(oldfd);
    if (!oldPipe) return 0;

    // If newfd is already open, close it first
    Pipe* existing = getPipe(newfd);
    if (existing && existing->ownerId == _state->id) {
        closePipe(newfd);
    }

    Pipe* newPipe = new Pipe();
    newPipe->id = newfd;
    newPipe->ownerId = _state->id;
    newPipe->source = oldPipe;
    newPipe->flags = flags;
    if (flags & 0x80000) { // O_CLOEXEC
        newPipe->flags |= 0x80000;
    }

    TaskState::_allPipes.push(newPipe);
    _state->ownedPipeIds.push(newPipe->id);
    _state->pipes.push(newPipe);

    return newPipe->id;
}

bool Task::poll(Pipe* pipe) {
    pipe = getRealPipe(pipe);
    if (!pipe) return false;
    return pipe->position < pipe->writePosition;
}

PipeEntry* Task::poll(Pipe* pipe, void* outBuf) {
    pipe = getRealPipe(pipe);
    if (!pipe || !outBuf) return nullptr;
    if (pipe->position >= pipe->writePosition) {
        if (!pipe->blocking) return nullptr;
        return nullptr; // Blocking: caller should yield and retry
    }
    // Find the entry containing data at current position
    for (usz i = 0; i < pipe->entries.size(); ++i) {
        PipeEntry& entry = pipe->entries[i];
        if (entry.position + entry.size > pipe->position && entry.position <= pipe->position) {
            usz offset = pipe->position - entry.position;
            usz available = entry.size - offset;
            std::memcpy(outBuf, entry.data + offset, available);
            pipe->position += available;
            return &entry;
        }
    }
    return nullptr;
}

bool Task::poll(Pipe* pipe, Func<void()> fn) {
    pipe = getRealPipe(pipe);
    if (!pipe || !fn) return false;
    if (pipe->position < pipe->writePosition) {
        fn();
        return true;
    }
    return false;
}

void Task::seek(Pipe* pipe, usz pos) {
    pipe = getRealPipe(pipe);
    if (!pipe) return;
    pipe->position = pos;
}

long Task::fcntl(Pipe* pipe, int cmd, long arg) {
    pipe = getRealPipe(pipe);
    if (!pipe) return -1;
    switch (cmd) {
        case 1: // F_GETFD
            return (pipe->flags & 0x80000) ? 1 : 0; // FD_CLOEXEC
        case 2: // F_SETFD
            if (arg & 1) pipe->flags |= 0x80000;
            else pipe->flags &= ~0x80000;
            return 0;
        case 3: // F_GETFL
            return pipe->flags & 0xFFF; // Return status flags
        case 4: // F_SETFL
            pipe->flags = (pipe->flags & ~0xFFF) | ((int)arg & 0xFFF);
            pipe->blocking = !(pipe->flags & 0x800); // O_NONBLOCK
            return 0;
        default:
            return -1;
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

void Task::onFetch(Func<void(usz, usz)> cb) {
    onFetch(0, (usz)-1, cb);
}

void Task::onFetch(usz start, usz end, Func<void(usz, usz)> cb) {
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
    _state->originalFetchRanges.push(fr);
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
                AOT::invalidate(_state->aotCache, reinterpret_cast<usz>(r.physical), r.size);
                Task::releaseAllocation(r.physical);
            }
            usz last = _state->regions.size() - 1;
            if ((usz)i != last) {
                _state->regions[(usz)i] = _state->regions[last];
            }
            _state->regions.pop();
        }
    }
    xi_invalidate_sfi_cache();
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
                    AOT::invalidate(_state->aotCache, reinterpret_cast<usz>(r.physical), r.size);
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
    xi_invalidate_sfi_cache();
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

void Task::registerAllocation(u8* ptr, usz size, bool isMmap) {
    if (!ptr) return;
    ensureInitialized();
    PhysicalAllocation alloc;
    alloc.ptr = ptr;
    alloc.size = size;
    alloc.refCount = 1;
    alloc.isMmap = isMmap;
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
                if (alloc.isMmap) {
#ifndef _WIN32
                    usz ptrVal = reinterpret_cast<usz>(alloc.ptr);
                    usz mapAddr = ptrVal & ~4095;
                    usz mapLen = (alloc.size + (ptrVal - mapAddr) + 4095) & ~4095;
                    ::munmap(reinterpret_cast<void*>(mapAddr), mapLen);
#endif
                } else {
                    delete[] alloc.ptr;
                }
                
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

usz Task::getAllocationRefCount(u8* ptr) {
    if (!ptr) return 0;
    ensureInitialized();
    for (usz i = 0; i < _allocations.size(); ++i) {
        PhysicalAllocation& alloc = _allocations[i];
        if (ptr >= alloc.ptr && ptr < alloc.ptr + alloc.size) {
            return alloc.refCount;
        }
    }
    return 0;
}

TaskState* Task::allocTask(usz parentId) {
    ensureInitialized();
    TaskState* s = new TaskState();
    s->id = _nextTaskId++;
    s->parentId = parentId;
    s->status = TaskStatus::Created;
    s->quotaUs = 0;

    if (parentId < _tasks.size() && _tasks[parentId]) {
        TaskState* parent = _tasks[parentId];
        s->bannedList = parent->bannedList;
    } else {
        s->bannedList.push("hlt");
        s->bannedList.push("cli");
        s->bannedList.push("sti");
        s->bannedList.push("int");
        s->bannedList.push("into");
        s->bannedList.push("in");
        s->bannedList.push("out");
        s->bannedList.push("retf");
        s->bannedList.push("iret");
        s->bannedList.push("far_call");
        s->bannedList.push("mov_seg");
        s->bannedList.push("vex");
        s->bannedList.push("evex");
        s->bannedList.push("sysret");
        s->bannedList.push("wrmsr");
        s->bannedList.push("rdmsr");
        s->bannedList.push("sysenter");
        s->bannedList.push("sysexit");
        s->bannedList.push("priv_seg");
        s->bannedList.push("clts");
        s->bannedList.push("invd");
        s->bannedList.push("wbinvd");
        s->bannedList.push("mov_cr_dr");
        s->bannedList.push("pop_fs_gs");
        s->bannedList.push("lss");
        s->bannedList.push("lfs");
        s->bannedList.push("lgs");
        s->bannedList.push("rsm");
        s->bannedList.push("fsgsbase");
        s->bannedList.push("syscall");
    }

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

    // Destroy all pipes owned by this task
    {
        Array<usz> pipeIds = s->ownedPipeIds; // Copy to avoid modification during iteration
        for (usz i = 0; i < pipeIds.size(); ++i) {
            Task t(s);
            t.closePipe(pipeIds[i]);
        }
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
                xi_assign_tid_if_needed(current);
                current->status = TaskStatus::Running;
                resetPeriod(coreId);
            }
            proposeFrequency(coreId);
            return;
        }

        if (next == current) {
            xi_assign_tid_if_needed(next);
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

        xi_assign_tid_if_needed(next);
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
                        xi_assign_tid_if_needed(current);
                        current->status = TaskStatus::Running;
                    }
                    proposeFrequency(coreId);
                    return;
                }
            } else {
                if (current) {
                    xi_assign_tid_if_needed(current);
                    current->status = TaskStatus::Running;
                }
                proposeFrequency(coreId);
                return;
            }
        }

        xi_assign_tid_if_needed(next);
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
#ifndef _WIN32
        ::syscall(158, 0x1003, &g_host_fs_base); // ARCH_GET_FS
#endif
        // Create the root task (id=0).
        TaskState* root = new TaskState();
        root->id = 0;
        root->parentId = 0;
        xi_assign_tid_if_needed(root);
        root->status = TaskStatus::Running;
        root->quotaUs = 0; // Unlimited.

        root->bannedList.push("hlt");
        root->bannedList.push("cli");
        root->bannedList.push("sti");
        root->bannedList.push("int");
        root->bannedList.push("into");
        root->bannedList.push("in");
        root->bannedList.push("out");
        root->bannedList.push("retf");
        root->bannedList.push("iret");
        root->bannedList.push("far_call");
        root->bannedList.push("mov_seg");
        root->bannedList.push("vex");
        root->bannedList.push("evex");
        root->bannedList.push("sysret");
        root->bannedList.push("wrmsr");
        root->bannedList.push("rdmsr");
        root->bannedList.push("sysenter");
        root->bannedList.push("sysexit");
        root->bannedList.push("priv_seg");
        root->bannedList.push("clts");
        root->bannedList.push("invd");
        root->bannedList.push("wbinvd");
        root->bannedList.push("mov_cr_dr");
        root->bannedList.push("pop_fs_gs");
        root->bannedList.push("lss");
        root->bannedList.push("lfs");
        root->bannedList.push("lgs");
        root->bannedList.push("rsm");
        root->bannedList.push("fsgsbase");
        root->bannedList.push("syscall");

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
        std::printf("[Task::reset] Deleting allocation %lu: ptr=%p, size=%lu, refCount=%d\n",
                    (unsigned long)i, _allocations[i].ptr, (unsigned long)_allocations[i].size, (int)_allocations[i].refCount);
        std::fflush(stdout);
        if (_allocations[i].isMmap) {
#ifndef _WIN32
            usz ptrVal = reinterpret_cast<usz>(_allocations[i].ptr);
            usz mapAddr = ptrVal & ~4095;
            usz mapLen = (_allocations[i].size + (ptrVal - mapAddr) + 4095) & ~4095;
            ::munmap(reinterpret_cast<void*>(mapAddr), mapLen);
#endif
        } else {
            delete[] _allocations[i].ptr;
        }
    }
    _allocations.clear();

    _cores.clear();

    _nextTaskId = 1;
    _nextTid = 1;
    _schedulePeriodUs = 10000;
    _interruptIntervalUs = 1000;
    _onChangeFrequency._clear();
    instance = nullptr;

    // Clean up all pipes
    for (usz i = 0; i < TaskState::_allPipes.size(); ++i) {
        delete TaskState::_allPipes[i];
    }
    TaskState::_allPipes.clear();
    TaskState::_nextPipeId = 3;

    ensureInitialized();
}

static void xi_destroy_aot_cache_recursive(TaskState* state) {
    if (!state) return;
    AOT::destroyCache(state->aotCache);
    for (usz i = 0; i < state->childIds.size(); ++i) {
        Task child = Task::findTask(state->childIds[i]);
        if (child.valid()) {
            xi_destroy_aot_cache_recursive(child._state);
        }
    }
}

void Task::onInstruction(const String& instruction, Func<void()> callback) {
    if (!_state) return;
    Task caller = Task::current();
    bool isSelf = (caller.valid() && _state->id == caller.id());
    bool isParent = (caller.valid() && _state->parentId == caller.id());
    bool isKernel = (!caller.valid() || caller.id() == 0);

    if (!isSelf && !isParent && !isKernel) {
        return; // Blocked: not self, not parent, not kernel.
    }

    // Ensure unique copy of instructionHooks
    {
        Array<TaskState::InstructionHook> uniqueHooks;
        for (usz i = 0; i < _state->instructionHooks.size(); ++i) {
            uniqueHooks[i] = _state->instructionHooks[i];
        }
        _state->instructionHooks = uniqueHooks;
    }

    _state->isInstructionIsolated = true;

    bool found = false;
    for (usz i = 0; i < _state->instructionHooks.size(); ++i) {
        if (_state->instructionHooks[i].name == instruction) {
            _state->instructionHooks[i].callback = callback;
            if (!isSelf) {
                _state->instructionHooks[i].banned = false;
            }
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
    xi_destroy_aot_cache_recursive(_state);
}

void Task::onInstruction(const Array<String>& instructions, Func<void()> callback) {
    for (usz i = 0; i < instructions.size(); ++i) {
        onInstruction(instructions[i], callback);
    }
}

void Task::offInstruction(const String& instruction) {
    if (!_state) return;
    Task caller = Task::current();
    bool isSelf = (caller.valid() && _state->id == caller.id());
    bool isParent = (caller.valid() && _state->parentId == caller.id());
    bool isKernel = (!caller.valid() || caller.id() == 0);

    if (!isSelf && !isParent && !isKernel) {
        return; // Blocked: not self, not parent, not kernel.
    }

    // Ensure unique copy of instructionHooks
    {
        Array<TaskState::InstructionHook> uniqueHooks;
        for (usz i = 0; i < _state->instructionHooks.size(); ++i) {
            uniqueHooks[i] = _state->instructionHooks[i];
        }
        _state->instructionHooks = uniqueHooks;
    }

    _state->isInstructionIsolated = true;

    bool found = false;
    for (usz i = 0; i < _state->instructionHooks.size(); ++i) {
        if (_state->instructionHooks[i].name == instruction) {
            _state->instructionHooks[i].callback = Func<void()>();
            _state->instructionHooks[i].banned = false;
            found = true;
            break;
        }
    }
    if (!found) {
        TaskState::InstructionHook hook;
        hook.name = instruction;
        hook.callback = Func<void()>();
        hook.banned = false;
        _state->instructionHooks.push(hook);
    }
    xi_destroy_aot_cache_recursive(_state);
}

void Task::trapInstruction(const String& instruction) {
    if (!_state) return;
    Task caller = Task::current();
    bool isSelf = (caller.valid() && _state->id == caller.id());
    bool isParent = (caller.valid() && _state->parentId == caller.id());
    bool isKernel = (!caller.valid() || caller.id() == 0);

    if (!isSelf && !isParent && !isKernel) {
        return; // Blocked: not self, not parent, not kernel.
    }

    // Ensure unique copy of instructionHooks
    {
        Array<TaskState::InstructionHook> uniqueHooks;
        for (usz i = 0; i < _state->instructionHooks.size(); ++i) {
            uniqueHooks[i] = _state->instructionHooks[i];
        }
        _state->instructionHooks = uniqueHooks;
    }

    _state->isInstructionIsolated = true;

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
    xi_destroy_aot_cache_recursive(_state);
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

void Task::onInstructionTranslate(const String& instruction, Func<Array<u8>(const Array<u8>&)> callback) {
    if (!_state) return;
    Task caller = Task::current();
    bool isSelf = (caller.valid() && _state->id == caller.id());
    bool isParent = (caller.valid() && _state->parentId == caller.id());
    bool isKernel = (!caller.valid() || caller.id() == 0);

    if (!isSelf && !isParent && !isKernel) {
        return;
    }

    // Ensure unique copy of instructionTranslators
    {
        Array<TaskState::InstructionTranslator> uniqueTranslators;
        for (usz i = 0; i < _state->instructionTranslators.size(); ++i) {
            uniqueTranslators[i] = _state->instructionTranslators[i];
        }
        _state->instructionTranslators = uniqueTranslators;
    }

    _state->isInstructionIsolated = true;

    bool found = false;
    for (usz i = 0; i < _state->instructionTranslators.size(); ++i) {
        if (_state->instructionTranslators[i].name == instruction) {
            _state->instructionTranslators[i].callback = callback;
            found = true;
            break;
        }
    }
    if (!found) {
        TaskState::InstructionTranslator translator;
        translator.name = instruction;
        translator.callback = callback;
        _state->instructionTranslators.push(translator);
    }
    xi_destroy_aot_cache_recursive(_state);
}


void Task::onSwap(Func<void(usz, usz)> cb) {
    if (!_state) return;
    Task caller = Task::current();
    if (caller.valid() && caller.id() != 0) {
        if (_state->parentId != caller.id() && _state->id != caller.id()) {
            return; // Blocked
        }
    }
    _state->swapCallback = cb;
}

void Task::onSwap(usz start, usz end, Func<void(usz, usz)> cb) {
    if (!_state) return;
    Task caller = Task::current();
    if (caller.valid() && caller.id() != 0) {
        if (_state->parentId != caller.id() && _state->id != caller.id()) {
            return; // Blocked
        }
    }
    TaskState::SwapRange sr;
    sr.start = start;
    sr.end = end;
    sr.callback = cb;
    _state->swapRanges.push(sr);
}

void Task::onStore(Func<void(usz, usz)> cb) {
    onStore(0, (usz)-1, cb);
}

void Task::onStore(usz start, usz end, Func<void(usz, usz)> cb) {
    if (!_state) return;
    Task caller = Task::current();
    if (caller.valid() && caller.id() != 0) {
        if (_state->parentId != caller.id()) {
            return; // Blocked: only parent can set callbacks.
        }
    }
    TaskState::StoreRange sr;
    sr.start = start;
    sr.end = end;
    sr.callback = cb;
    _state->storeRanges.push(sr);
}

void Task::setMaxChildrenMemory(usz bytes) {
    if (!_state) return;
    Task caller = Task::current();
    if (caller.valid() && caller.id() != 0) {
        if (_state->parentId != caller.id() && _state->id != caller.id()) {
            return;
        }
    }
    _state->maxChildrenMemory = bytes;
}

usz Task::totalChildrenMemory() const {
    if (!_state) return 0;
    usz total = 0;
    for (usz c = 0; c < _state->childIds.size(); ++c) {
        Task child = Task::findTask(_state->childIds[c]);
        if (child.valid()) {
            for (usz i = 0; i < child._state->regions.size(); ++i) {
                total += child._state->regions[i].size;
            }
        }
    }
    return total;
}

void xi_reset_task_state_for_tests() {
    Task::reset();
}

} // namespace Task

