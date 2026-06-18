/**
 * @file AOT_Amd64.cpp
 * @brief x86_64 AOT SFI (Software Fault Isolation) binary rewriter.
 *
 * Implements the AOT class for the AMD64 architecture. The rewriter:
 *   1. Decodes x86_64 instructions using a basic length decoder.
 *   2. For instructions with memory operands (loads/stores), inserts a
 *      CALL to a bounds-check stub before the original instruction.
 *   3. For relative JMP/CALL instructions, adjusts the displacement
 *      to account for the expanded code layout.
 *   4. Unconditionally bans privileged instructions (syscall, int, sysenter,
 *      hlt, cli, sti, in, out, etc.) to prevent container escape.
 *   5. Instruments indirect JMP/CALL and RET with jump-target validation
 *      to prevent execution of arbitrary addresses.
 *   6. Validates both readable and writable regions in bounds checks.
 *
 * Mathematical containment guarantee:
 *   After AOT rewriting, a task can ONLY:
 *     - Access memory within its own MemoryRegion set
 *     - Execute code within its own executable regions
 *     - Never execute privileged instructions
 *     - Never escape via stack overflow (stack bounds checked)
 *     - Never escape via indirect jumps (targets validated)
 *     - Never escape via return addresses (ret rewritten as validated jump)
 */

#if defined(__x86_64__) || defined(_M_X64)

#include "../../../include/Task/AOT.hpp"
#include "../../../include/Task/Task.hpp"

#include <cstring>
#include <cstdlib>

#ifndef _WIN32
#include <sys/mman.h>
#include <cerrno>
#include <cstdio>
#endif

namespace Task {

// -------------------------------------------------------------------------
// SFI Bounds-Check Functions
// -------------------------------------------------------------------------

/**
 * @brief Validates that a memory access [addr, addr+size) falls within
 *        the task's accessible memory regions (both readable AND writable).
 *
 * If the access is out-of-bounds, traps with ud2.
 */
thread_local usz tl_last_task_id = (usz)-1;

// Read/Write bounds cache
thread_local u8* tl_last_phys_base = nullptr;
thread_local u8* tl_last_phys_limit = nullptr;
thread_local usz tl_last_virt_base = 0;
thread_local usz tl_last_virt_limit = 0;

// Write bounds cache
thread_local u8* tl_last_write_phys_base = nullptr;
thread_local u8* tl_last_write_phys_limit = nullptr;
thread_local usz tl_last_write_virt_base = 0;
thread_local usz tl_last_write_virt_limit = 0;

extern "C" void xi_invalidate_sfi_cache() {
    tl_last_task_id = (usz)-1;
    tl_last_phys_base = nullptr;
    tl_last_phys_limit = nullptr;
    tl_last_virt_base = 0;
    tl_last_virt_limit = 0;
    tl_last_write_phys_base = nullptr;
    tl_last_write_phys_limit = nullptr;
    tl_last_write_virt_base = 0;
    tl_last_write_virt_limit = 0;
}

extern "C" void xi_sfi_bounds_check(void* addr, usz size) {
    xi_last_jit_rip = reinterpret_cast<usz>(__builtin_return_address(0));
    if (addr == nullptr && size == 0) return;
    TaskState* state = xi_get_current_task();
    if (!state) return;

    u8* target = static_cast<u8*>(addr);
    usz targetAddr = reinterpret_cast<usz>(addr);

    // 0. Check thread-local cache
    if (state->id == tl_last_task_id) {
        if ((target >= tl_last_phys_base && target + size <= tl_last_phys_limit) ||
            (targetAddr >= tl_last_virt_base && targetAddr + size <= tl_last_virt_limit)) {
            return; // Cache hit!
        }
    } else {
        xi_invalidate_sfi_cache();
        tl_last_task_id = state->id;
    }

    // 1. Check existing mapped regions (both physical and virtual base)
    for (usz i = 0; i < state->regions.size(); ++i) {
        MemoryRegion& r = state->regions[i];
        if (r.physical) {
            if ((target >= r.physical && target + size <= r.physical + r.size) ||
                (targetAddr >= r.base && targetAddr + size <= r.base + r.size)) {
                r.lastAccessTicks = ++state->accessCounter;
                // Update cache
                tl_last_phys_base = r.physical;
                tl_last_phys_limit = r.physical + r.size;
                tl_last_virt_base = r.base;
                tl_last_virt_limit = r.base + r.size;
                return; // Access is within a valid region.
            }
        }
    }

    // 2. Check stack
    if (state->stack && target >= state->stack &&
        target + size <= state->stack + state->stackSize) {
        // Update cache with stack boundaries
        tl_last_phys_base = state->stack;
        tl_last_phys_limit = state->stack + state->stackSize;
        tl_last_virt_base = reinterpret_cast<usz>(state->stack);
        tl_last_virt_limit = reinterpret_cast<usz>(state->stack) + state->stackSize;
        return;
    }

    // 3. Check registered fetch ranges
    for (usz i = 0; i < state->fetchRanges.size(); ++i) {
        TaskState::FetchRange fr = state->fetchRanges[i];
        if (targetAddr >= fr.start && targetAddr + size <= fr.end) {
            TaskState* prev = xi_get_current_task();
            xi_set_current_task(nullptr);
            Task(state).stop(1);
            fr.callback(fr.start, fr.end);
            xi_set_current_task(prev);
            // Re-verify that the callback successfully mapped/allocated the region containing targetAddr
            for (usz j = 0; j < state->regions.size(); ++j) {
                MemoryRegion& r = state->regions[j];
                if (r.physical && targetAddr >= r.base && targetAddr + size <= r.base + r.size) {
                    return; // Successfully resolved!
                }
            }
        }
    }

    // Out-of-bounds access: trap.
    __asm__ volatile("ud2");
}

/**
 * @brief Validates that a WRITE access falls within a writable region.
 *
 * Separate from bounds_check because read-only regions (executable code,
 * parent's read-only mappings) must not be written to.
 */
extern "C" void xi_sfi_write_check(void* addr, usz size) {
    xi_last_jit_rip = reinterpret_cast<usz>(__builtin_return_address(0));
    if (addr == nullptr && size == 0) return;
    TaskState* state = xi_get_current_task();
    if (!state) return;

    u8* target = static_cast<u8*>(addr);
    usz targetAddr = reinterpret_cast<usz>(addr);

    // 0. Check thread-local write cache
    if (state->id == tl_last_task_id) {
        if ((target >= tl_last_write_phys_base && target + size <= tl_last_write_phys_limit) ||
            (targetAddr >= tl_last_write_virt_base && targetAddr + size <= tl_last_write_virt_limit)) {
            return; // Cache hit!
        }
    } else {
        xi_invalidate_sfi_cache();
        tl_last_task_id = state->id;
    }

    // 0.5. Check CoW regions and split if write is requested
    for (usz i = 0; i < state->regions.size(); ++i) {
        MemoryRegion& r = state->regions[i];
        if (r.physical && r.cow) {
            if ((target >= r.physical && target + size <= r.physical + r.size) ||
                (targetAddr >= r.base && targetAddr + size <= r.base + r.size)) {
                u8* oldPhys = r.physical;
                usz sz = r.size;
                if (Task::getAllocationRefCount(oldPhys) <= 1) {
                    r.writable = true;
                    r.cow = false;
                } else {
                    u8* newPhys = new u8[sz];
                    std::memcpy(newPhys, oldPhys, sz);
                    
                    Task::registerAllocation(newPhys, sz, false);
                    r.physical = newPhys;
                    r.writable = true;
                    r.cow = false;
                    
                    Task::releaseAllocation(oldPhys);
                }
                
                xi_invalidate_sfi_cache();
                return;
            }
        }
    }

    // 1. Check existing writable mapped regions
    for (usz i = 0; i < state->regions.size(); ++i) {
        MemoryRegion& r = state->regions[i];
        if (r.physical && r.writable) {
            if ((target >= r.physical && target + size <= r.physical + r.size) ||
                (targetAddr >= r.base && targetAddr + size <= r.base + r.size)) {
                r.lastAccessTicks = ++state->accessCounter;
                // Update write cache
                tl_last_write_phys_base = r.physical;
                tl_last_write_phys_limit = r.physical + r.size;
                tl_last_write_virt_base = r.base;
                tl_last_write_virt_limit = r.base + r.size;
                return; // Write is within a writable region.
            }
        }
    }

    // 2. Stack is always writable.
    if (state->stack && target >= state->stack &&
        target + size <= state->stack + state->stackSize) {
        // Update write cache with stack boundaries
        tl_last_write_phys_base = state->stack;
        tl_last_write_phys_limit = state->stack + state->stackSize;
        tl_last_write_virt_base = reinterpret_cast<usz>(state->stack);
        tl_last_write_virt_limit = reinterpret_cast<usz>(state->stack) + state->stackSize;
        return;
    }

    // 2.5 Check registered store ranges (onStore)
    for (usz i = 0; i < state->storeRanges.size(); ++i) {
        TaskState::StoreRange sr = state->storeRanges[i];
        if (targetAddr >= sr.start && targetAddr + size <= sr.end) {
            TaskState* prev = xi_get_current_task();
            xi_set_current_task(nullptr);
            Task(state).stop(1);
            sr.callback(sr.start, sr.end);
            xi_set_current_task(prev);
            // Re-verify that the callback successfully mapped/allocated the region and it is writable
            for (usz j = 0; j < state->regions.size(); ++j) {
                MemoryRegion& r = state->regions[j];
                if (r.physical && r.writable && targetAddr >= r.base && targetAddr + size <= r.base + r.size) {
                    return; // Successfully resolved!
                }
            }
        }
    }

    // 3. Check registered fetch ranges
    for (usz i = 0; i < state->fetchRanges.size(); ++i) {
        TaskState::FetchRange fr = state->fetchRanges[i];
        if (targetAddr >= fr.start && targetAddr + size <= fr.end) {
            TaskState* prev = xi_get_current_task();
            xi_set_current_task(nullptr);
            Task(state).stop(1);
            fr.callback(fr.start, fr.end);
            xi_set_current_task(prev);
            // Re-verify that the callback successfully mapped/allocated the region and it is writable
            for (usz j = 0; j < state->regions.size(); ++j) {
                MemoryRegion& r = state->regions[j];
                if (r.physical && r.writable && targetAddr >= r.base && targetAddr + size <= r.base + r.size) {
                    return; // Successfully resolved!
                }
            }
        }
    }

    // Write to non-writable region: trap.
    __asm__ volatile("ud2");
}

/**
 * @brief Validates that a jump/call target address falls within the task's
 *        executable memory regions or the current AOT'd code buffer.
 *
 * This is the mathematical guarantee against execution escape:
 * a task can NEVER execute code outside its own executable regions.
 */
extern "C" void xi_sfi_jump_check(void* target) {
    TaskState* state = xi_get_current_task();
    if (!state) return;

    u8* addr = static_cast<u8*>(target);

    // Check executable regions.
    for (usz i = 0; i < state->regions.size(); ++i) {
        MemoryRegion& r = state->regions[i];
        if (r.physical && r.executable) {
            if (addr >= r.physical && addr < r.physical + r.size) {
                return; // Valid executable target.
            }
        }
    }

    // Check AOT cache — the task may be jumping within rewritten code.
    for (usz i = 0; i < state->aotCache.size(); ++i) {
        AOTRegion& aot = state->aotCache[i];
        if (aot.patchedCode) {
            if (addr >= aot.patchedCode && addr < aot.patchedCode + aot.patchedSize) {
                return; // Valid AOT'd code target.
            }
        }
    }

    // Also allow jumping to the task's entry trampoline and context switch
    // mechanisms — these are kernel-managed addresses set up by the scheduler.
    // The entry function and trampoline are stored in entryFn.
    if (state->entryFn) {
        u8* entryAddr = reinterpret_cast<u8*>(state->entryFn);
        // Allow a generous range around the entry point (the trampoline
        // and its helper functions).
        if (addr >= entryAddr && addr < entryAddr + 4096) {
            return;
        }
    }

    u8* trampolineAddr = reinterpret_cast<u8*>(xi_context_entry_trampoline);
    if (addr >= trampolineAddr && addr < trampolineAddr + 4096) {
        return;
    }

    // Also allow jumping to host runner (parent task 0) executable regions.
    TaskState* parent = (Task::_tasks.size() > 0) ? Task::_tasks[0] : nullptr;
    if (parent) {
        for (usz i = 0; i < parent->regions.size(); ++i) {
            MemoryRegion& r = parent->regions[i];
            if (r.physical && r.executable) {
                if (addr >= r.physical && addr < r.physical + r.size) {
                    return;
                }
            }
        }
        for (usz i = 0; i < parent->aotCache.size(); ++i) {
            AOTRegion& aot = parent->aotCache[i];
            if (aot.patchedCode) {
                if (addr >= aot.patchedCode && addr < aot.patchedCode + aot.patchedSize) {
                    return;
                }
            }
        }
    }

    // Invalid jump target: trap.
    __asm__ volatile("ud2");
}

extern "C" void* xi_sfi_indirect_jump_resolver(void* target) {
    TaskState* state = xi_get_current_task();
    if (!state) return target;

    usz targetAddr = reinterpret_cast<usz>(target);

    // Print all regions for diagnostics
    ::printf("[xi_sfi_indirect_jump_resolver] Resolving targetAddr=0x%lx. Regions:\n", (unsigned long)targetAddr);
    for (usz i = 0; i < state->regions.size(); ++i) {
        MemoryRegion& r = state->regions[i];
        ::printf("  [%lu] base=0x%lx, size=0x%lx, physical=%p, exec=%d, owned=%d\n",
                 (unsigned long)i, (unsigned long)r.base, (unsigned long)r.size, r.physical, (int)r.executable, (int)r.owned);
    }
    ::fflush(stdout);

    // 1. Find the memory region containing this target
    MemoryRegion* targetRegion = nullptr;
    bool isVirtual = false;
    for (usz i = 0; i < state->regions.size(); ++i) {
        MemoryRegion& r = state->regions[i];
        if (r.physical) {
            // Check virtual
            if (targetAddr >= r.base && targetAddr < r.base + r.size) {
                targetRegion = &r;
                isVirtual = true;
                break;
            }
            // Check physical
            if (targetAddr >= reinterpret_cast<usz>(r.physical) &&
                targetAddr < reinterpret_cast<usz>(r.physical) + r.size) {
                targetRegion = &r;
                isVirtual = false;
                break;
            }
        }
    }

    if (!targetRegion) {
        // Check registered fetch ranges
        for (usz i = 0; i < state->fetchRanges.size(); ++i) {
            TaskState::FetchRange fr = state->fetchRanges[i];
            if (targetAddr >= fr.start && targetAddr < fr.end) {
                ::printf("[xi_sfi_indirect_jump_resolver] Target in fetchRange[%lu]: [0x%lx, 0x%lx)\n",
                         (unsigned long)i, (unsigned long)fr.start, (unsigned long)fr.end);
                ::fflush(stdout);
                TaskState* prevTask = xi_get_current_task();
                xi_set_current_task(nullptr);
                Task(state).stop(1);
                fr.callback(fr.start, fr.end);
                xi_set_current_task(prevTask);

                ::printf("[xi_sfi_indirect_jump_resolver] Callback finished. New Regions list:\n");
                for (usz j = 0; j < state->regions.size(); ++j) {
                    MemoryRegion& r = state->regions[j];
                    ::printf("  [%lu] base=0x%lx, size=0x%lx, physical=%p\n",
                             (unsigned long)j, (unsigned long)r.base, (unsigned long)r.size, r.physical);
                }
                ::fflush(stdout);

                // Re-verify if it's now mapped
                for (usz j = 0; j < state->regions.size(); ++j) {
                    MemoryRegion& r = state->regions[j];
                    if (r.physical && targetAddr >= r.base && targetAddr < r.base + r.size) {
                        targetRegion = &r;
                        isVirtual = true;
                        ::printf("[xi_sfi_indirect_jump_resolver] Re-verify MATCHED j=%lu (base=0x%lx, physical=%p), targetRegion=%p\n",
                                 (unsigned long)j, (unsigned long)r.base, r.physical, (void*)targetRegion);
                        ::fflush(stdout);
                        break;
                    }
                }
                if (targetRegion) break;
            }
        }
    }

    if (!targetRegion) {
        // Not in any memory region. Check if it's the entry point or trampoline.
        if (state->entryFn) {
            u8* entryAddr = reinterpret_cast<u8*>(state->entryFn);
            if (targetAddr >= reinterpret_cast<usz>(entryAddr) &&
                targetAddr < reinterpret_cast<usz>(entryAddr) + 4096) {
                return target;
            }
        }
        // Also check if it's already a patched address in the AOT cache!
        for (usz i = 0; i < state->aotCache.size(); ++i) {
            AOTRegion& aot = state->aotCache[i];
            if (aot.patchedCode && targetAddr >= reinterpret_cast<usz>(aot.patchedCode) &&
                targetAddr < reinterpret_cast<usz>(aot.patchedCode) + aot.patchedSize) {
                return target;
            }
        }
        // Also check if it's a host runner (parent task 0) executable region!
        TaskState* parent = (Task::_tasks.size() > 0) ? Task::_tasks[0] : nullptr;
        if (parent) {
            for (usz i = 0; i < parent->regions.size(); ++i) {
                MemoryRegion& r = parent->regions[i];
                if (r.physical && r.executable) {
                    if (targetAddr >= reinterpret_cast<usz>(r.physical) &&
                        targetAddr < reinterpret_cast<usz>(r.physical) + r.size) {
                        return target;
                    }
                }
            }
            for (usz i = 0; i < parent->aotCache.size(); ++i) {
                AOTRegion& aot = parent->aotCache[i];
                if (aot.patchedCode && targetAddr >= reinterpret_cast<usz>(aot.patchedCode) &&
                    targetAddr < reinterpret_cast<usz>(aot.patchedCode) + aot.patchedSize) {
                    return target;
                }
            }
        }
        // Invalid target: trap!
        __asm__ volatile("ud2");
        return nullptr;
    }

    // 2. We found the region. Ensure it is AOT-compiled.
    targetRegion->lastAccessTicks = ++state->accessCounter;
    MemoryRegion targetRegionVal = *targetRegion;
    AOTRegion* cached = AOT::findCached(state->aotCache, reinterpret_cast<usz>(targetRegionVal.physical), targetRegionVal.size);
    if (!cached) {
        // Run AOT compilation!
        targetRegion->executable = true; // Make sure it is marked executable
        AOTResult res = AOT::rewrite(targetRegionVal.physical, targetRegionVal.size, state->regions, targetRegionVal.base);
        if (res.success && res.patchedCode) {
            AOTRegion reg;
            reg.originalAddr = reinterpret_cast<usz>(targetRegionVal.physical);
            reg.originalSize = targetRegionVal.size;
            reg.patchedCode = res.patchedCode;
            reg.patchedSize = res.patchedSize;
            reg.offsetMap = res.offsetMap;
            state->aotCache.push(reg);
            cached = &state->aotCache[state->aotCache.size() - 1];
        }
    }

    if (cached && cached->patchedCode && cached->offsetMap) {
        usz offset = isVirtual ? (targetAddr - targetRegionVal.base) : (targetAddr - reinterpret_cast<usz>(targetRegionVal.physical));
        if (offset < cached->originalSize && cached->offsetMap[offset] != 0xFFFFFFFF) {
            return cached->patchedCode + cached->offsetMap[offset];
        } else {
            ::printf("[xi_sfi_indirect_jump_resolver] Failed mapping check:\n"
                     "  targetAddr=0x%lx\n"
                     "  isVirtual=%d\n"
                     "  r.base=0x%lx, r.physical=%p, r.size=0x%lx\n"
                     "  offset=%lu (0x%lx)\n"
                     "  cached->originalSize=%lu\n"
                     "  mapVal=0x%x\n",
                     (unsigned long)targetAddr, (int)isVirtual,
                     (unsigned long)targetRegionVal.base, targetRegionVal.physical, (unsigned long)targetRegionVal.size,
                     (unsigned long)offset, (unsigned long)offset,
                     (unsigned long)cached->originalSize,
                     cached->offsetMap ? cached->offsetMap[offset < cached->originalSize ? offset : 0] : 0);
            ::fflush(stdout);
        }
    } else {
        ::printf("[xi_sfi_indirect_jump_resolver] No cache or invalid: cached=%p, patched=%p, map=%p\n",
                 cached, cached ? cached->patchedCode : nullptr, cached ? cached->offsetMap : nullptr);
        ::fflush(stdout);
    }

    // If compilation failed, trap!
    __asm__ volatile("ud2");
    return nullptr;
}


/**
 * @brief Validates the stack pointer is within the task's stack bounds.
 *
 * Called after any instruction that modifies RSP to ensure the stack
 * hasn't underflowed or overflowed past the allocated stack region.
 */
extern "C" void xi_sfi_stack_check(void* rsp_value) {
    TaskState* state = xi_get_current_task();
    if (!state) return;
    if (!state->stack) return;

    u8* sp = static_cast<u8*>(rsp_value);
    u8* stackBase = state->stack;
    u8* stackTop = state->stack + state->stackSize;

    if (sp < stackBase || sp > stackTop) {
        // Stack bounds violation: trap.
        __asm__ volatile("ud2");
    }
}

extern "C" void xi_emulate_syscall(TaskState* state, GuestRegs* regs);

extern "C" void xi_emulate_syscall_helper(void* statePtr, GuestRegs* regs) {
    if (regs) {
        xi_last_guest_rbx = regs->rbx;
        xi_guest_regs = regs;
    }
    if (statePtr) {
        xi_emulate_syscall(static_cast<TaskState*>(statePtr), regs);
    }
    xi_guest_regs = nullptr;
}

extern "C" void xi_run_instruction_callback(void* callbackPtr, GuestRegs* regs) {
    if (regs) {
        xi_last_guest_rbx = regs->rbx;
        xi_guest_regs = regs;
    }
    if (callbackPtr) {
        auto* cb = static_cast<Func<void()>*>(callbackPtr);
        if (*cb) {
            (*cb)();
        }
    }
    xi_guest_regs = nullptr;
}

// -------------------------------------------------------------------------
// Privileged Instruction Detection
// -------------------------------------------------------------------------

/**
 * @brief Checks if an instruction is unconditionally banned (privileged).
 *
 * These instructions are ALWAYS banned regardless of instruction hooks.
 * A task can NEVER execute them after AOT rewriting.
 *
 * @return true if the instruction must be replaced with ud2.
 */
struct InstructionMatch {
    bool matched;
    bool banned;
    void* callbackPtr;
    bool isTranslate;
    void* translateCallbackPtr;
};

static String getInstructionName(const u8* code, usz len) {
    if (len == 0) return "";
    usz pos = 0;
    // Skip legacy prefixes
    for (;;) {
        if (pos >= len) return "";
        u8 b = code[pos];
        if (b == 0x66 || b == 0x67 || b == 0xF0 || b == 0xF2 || b == 0xF3 ||
            b == 0x2E || b == 0x36 || b == 0x3E || b == 0x26 || b == 0x64 || b == 0x65) {
            ++pos;
        } else break;
    }
    // Skip REX prefix
    if (pos < len && (code[pos] & 0xF0) == 0x40) {
        ++pos;
    }
    if (pos >= len) return "";

    u8 op = code[pos];
    usz remaining = len - pos;
    if (op == 0x90 && remaining == 1) return "nop";
    if (op == 0xF4 && remaining == 1) return "hlt";
    if (op == 0xFA && remaining == 1) return "cli";
    if (op == 0xFB && remaining == 1) return "sti";
    if (op == 0xCC && remaining == 1) return "int3";
    if (op == 0xCD) return "int";
    if (op == 0xCE && remaining == 1) return "into";
    if (op == 0xE4 || op == 0xE5 || op == 0xEC || op == 0xED) return "in";
    if (op == 0xE6 || op == 0xE7 || op == 0xEE || op == 0xEF) return "out";
    if (op == 0xCB || (op == 0xCA && remaining >= 3)) return "retf";
    if (op == 0xCF && remaining == 1) return "iret";
    if (op == 0x9A || op == 0xEA) return "far_call";
    if (op == 0x8E) return "mov_seg";
    if (op == 0xC4 || op == 0xC5) return "vex";
    if (op == 0x62) return "evex";
    if (op == 0xFF && remaining >= 2) {
        u8 modrm = code[pos + 1];
        u8 reg = (modrm >> 3) & 7;
        if (reg == 3 || reg == 5) return "far_call";
    }
    if (op == 0x0F && remaining >= 2) {
        u8 op2 = code[pos + 1];
        if (op2 == 0x05) return "syscall";
        if (op2 == 0xA2) return "cpuid";
        if (op2 == 0x31) return "rdtsc";
        if (op2 == 0x0B) return "ud2";
        if (op2 == 0x38 || op2 == 0x3A) return "vex";
        if (op2 == 0x07) return "sysret";
        if (op2 == 0x30) return "wrmsr";
        if (op2 == 0x32) return "rdmsr";
        if (op2 == 0x34) return "sysenter";
        if (op2 == 0x35) return "sysexit";
        if (op2 == 0x01) return "priv_seg";
        if (op2 == 0x06) return "clts";
        if (op2 == 0x08) return "invd";
        if (op2 == 0x09) return "wbinvd";
        if (op2 == 0x20 || op2 == 0x21 || op2 == 0x22 || op2 == 0x23) return "mov_cr_dr";
        if (op2 == 0xA1 || op2 == 0xA9) return "pop_fs_gs";
        if (op2 == 0xB2) return "lss";
        if (op2 == 0xB4) return "lfs";
        if (op2 == 0xB5) return "lgs";
        if (op2 == 0xAA) return "rsm";
        if (op2 == 0xAE && remaining >= 3) {
            u8 modrm = code[pos + 2];
            u8 mod = (modrm >> 6) & 3;
            u8 reg = (modrm >> 3) & 7;
            if (mod == 3 && reg <= 3) return "fsgsbase";
        }
    }
    return "";
}

static InstructionMatch checkInstructionHooks(const u8* code, usz len, const String& name) {
    InstructionMatch match = {false, false, nullptr, false, nullptr};
    TaskState* state = xi_get_current_task();
    if (!state) {
        Task root = Task::root();
        state = root._state;
    }
    if (!state) return match;

    if (name == "syscall") {
        if (len > 0) {
            u8 firstByte = code[0];
            if (firstByte == 0x66 || firstByte == 0x67 || firstByte == 0xF0 || firstByte == 0xF2 || firstByte == 0xF3 ||
                firstByte == 0x2E || firstByte == 0x36 || firstByte == 0x3E || firstByte == 0x26 || firstByte == 0x64 || firstByte == 0x65) {
                match.matched = true;
                match.banned = true;
                match.callbackPtr = nullptr; // Trap with ud2!
                return match;
            }
        }
        match.matched = true;
        match.banned = true;
        match.callbackPtr = reinterpret_cast<void*>(1);
        return match;
    }

    String op1;
    if (len >= 1) op1 = String(code, 1);
    String op2;
    if (len >= 2) op2 = String(code, 2);

    TaskState* curr = state;
    while (curr) {
        // 1. Check translators first
        for (usz i = 0; i < curr->instructionTranslators.size(); ++i) {
            const auto& t = curr->instructionTranslators[i];
            if (t.name == name || (op1.size() > 0 && t.name == op1) || (op2.size() > 0 && t.name == op2)) {
                match.matched = true;
                match.isTranslate = true;
                match.translateCallbackPtr = const_cast<void*>(reinterpret_cast<const void*>(&t.callback));
                return match;
            }
        }

        // 2. Check explicit hooks next
        bool foundHook = false;
        for (usz i = 0; i < curr->instructionHooks.size(); ++i) {
            const auto& hook = curr->instructionHooks[i];
            if (hook.name == name || (op1.size() > 0 && hook.name == op1) || (op2.size() > 0 && hook.name == op2)) {
                if (hook.callback.isValid() || hook.banned) {
                    match.matched = true;
                    match.banned = hook.banned;
                    match.callbackPtr = const_cast<void*>(reinterpret_cast<const void*>(&hook.callback));
                    return match;
                }
                foundHook = true;
                break;
            }
        }
        if (foundHook) {
            // Hook exists but has no valid callback and is not banned.
            // This means it was cleared/off'd. Traverse to parent.
            goto next_parent;
        }

        // 3. Check inherited ban list
        for (usz i = 0; i < curr->bannedList.size(); ++i) {
            const String& ban = curr->bannedList[i];
            if (ban == name || (op1.size() > 0 && ban == op1) || (op2.size() > 0 && ban == op2)) {
                match.matched = true;
                match.banned = true;
                return match;
            }
        }

    next_parent:
        if (curr->parentId < Task::_tasks.size() && Task::_tasks[curr->parentId] && curr->id != 0) {
            curr = Task::_tasks[curr->parentId];
        } else {
            curr = nullptr;
        }
    }

    return match;
}

// Banned instructions are checked dynamically.

// -------------------------------------------------------------------------
// Indirect Jump/Call Detection
// -------------------------------------------------------------------------

/**
 * @brief Detects if an instruction is an indirect JMP or CALL (via register
 *        or memory operand), or a RET instruction.
 *
 * These are the instructions that can transfer control to an arbitrary
 * address computed at runtime:
 *   - FF /2: CALL r/m64 (indirect call)
 *   - FF /4: JMP r/m64 (indirect jump)
 *   - C3:    RET (pop address from stack, jump to it)
 *   - C2:    RET imm16
 *   - CB:    RETF
 *   - CA:    RETF imm16
 *
 * @param code  Instruction bytes (after prefixes and REX).
 * @param len   Total instruction length.
 * @param outIsIndirectJump  Set to true if indirect JMP/CALL.
 * @param outIsRet           Set to true if RET.
 */
static void detectIndirectControl(const u8* code, usz len,
                                   bool& outIsIndirectJump,
                                   bool& outIsRet) {
    outIsIndirectJump = false;
    outIsRet = false;

    if (len == 0) return;

    // Scan past prefixes and REX to find the opcode.
    usz pos = 0;
    for (;;) {
        if (pos >= len) return;
        u8 b = code[pos];
        if (b == 0x66 || b == 0x67 || b == 0xF0 || b == 0xF2 || b == 0xF3 ||
            b == 0x2E || b == 0x36 || b == 0x3E || b == 0x26 || b == 0x64 || b == 0x65) {
            ++pos;
        } else break;
    }
    // REX prefix
    if (pos < len && (code[pos] & 0xF0) == 0x40) {
        ++pos;
    }
    if (pos >= len) return;

    u8 opcode = code[pos];

    // RET variants - only C3 is supported as validation RET
    if (opcode == 0xC3) {
        outIsRet = true;
        return;
    }

    // FF group (indirect JMP/CALL):
    // FF /2 = CALL r/m64
    // FF /3 = CALL far indirect
    // FF /4 = JMP r/m64
    // FF /5 = JMP far indirect
    // FF /6 = PUSH r/m64 (not a jump)
    if (opcode == 0xFF && pos + 1 < len) {
        u8 modrm = code[pos + 1];
        u8 reg = (modrm >> 3) & 7;
        if (reg == 2 || reg == 4) {
            outIsIndirectJump = true;
        }
    }
}

static void emitUd2(u8*& outPtr) {
    *outPtr++ = 0x0F;
    *outPtr++ = 0x0B;
}

static void emitCallbackCall(u8*& outPtr, u64 callbackPtr, u64 helperAddr) {
    // Save caller-saved registers and rbx
    *outPtr++ = 0x9C; // pushfq
    *outPtr++ = 0x50; // push rax
    *outPtr++ = 0x53; // push rbx
    *outPtr++ = 0x51; // push rcx
    *outPtr++ = 0x52; // push rdx
    *outPtr++ = 0x56; // push rsi
    *outPtr++ = 0x57; // push rdi
    *outPtr++ = 0x41; *outPtr++ = 0x50; // push r8
    *outPtr++ = 0x41; *outPtr++ = 0x51; // push r9
    *outPtr++ = 0x41; *outPtr++ = 0x52; // push r10
    *outPtr++ = 0x41; *outPtr++ = 0x53; // push r11

    // mov rsi, rsp
    *outPtr++ = 0x48;
    *outPtr++ = 0x89;
    *outPtr++ = 0xE6;

    // movabs rdi, callbackPtr
    *outPtr++ = 0x48;
    *outPtr++ = 0xBF;
    std::memcpy(outPtr, &callbackPtr, 8);
    outPtr += 8;

    // Align stack
    *outPtr++ = 0x55; // push rbp
    *outPtr++ = 0x48; *outPtr++ = 0x89; *outPtr++ = 0xE5; // mov rbp, rsp
    *outPtr++ = 0x48; *outPtr++ = 0x83; *outPtr++ = 0xE4; *outPtr++ = 0xF0; // and rsp, -16

    // movabs rax, helperAddr
    *outPtr++ = 0x48;
    *outPtr++ = 0xB8;
    std::memcpy(outPtr, &helperAddr, 8);
    outPtr += 8;

    // call rax
    *outPtr++ = 0xFF;
    *outPtr++ = 0xD0;

    // Restore stack
    *outPtr++ = 0xC9; // leave

    // Restore registers
    *outPtr++ = 0x41; *outPtr++ = 0x5B; // pop r11
    *outPtr++ = 0x41; *outPtr++ = 0x5A; // pop r10
    *outPtr++ = 0x41; *outPtr++ = 0x59; // pop r9
    *outPtr++ = 0x41; *outPtr++ = 0x58; // pop r8
    *outPtr++ = 0x5F; // pop rdi
    *outPtr++ = 0x5E; // pop rsi
    *outPtr++ = 0x5A; // pop rdx
    *outPtr++ = 0x59; // pop rcx
    *outPtr++ = 0x5B; // pop rbx
    *outPtr++ = 0x58; // pop rax
    *outPtr++ = 0x9D; // popfq
}

// -------------------------------------------------------------------------
// x86_64 Instruction Length Decoder
// -------------------------------------------------------------------------

/**
 * @brief Determines the length of an x86_64 instruction and whether it
 *        accesses memory (has a ModRM byte with mod != 11).
 *
 * This is a simplified decoder that handles:
 *   - Legacy prefixes (66, 67, F0, F2, F3, 2E, 36, 3E, 26, 64, 65)
 *   - REX prefixes (40-4F)
 *   - One-byte opcodes (most common)
 *   - Two-byte opcodes (0F xx)
 *   - ModRM and SIB bytes
 *   - Displacement (8-bit and 32-bit)
 *   - Immediate operands
 *
 * @param code         Pointer to the instruction bytes.
 * @param maxLen       Maximum bytes available to decode.
 * @param outHasMem    Set to true if the instruction has a memory operand.
 * @param outIsRelJmp  Set to true if the instruction is a relative JMP/CALL.
 * @param outRelOffset Offset of the relative displacement within the instruction.
 * @param outRelSize   Size of the relative displacement (1 or 4 bytes).
 * @param outIsRspModifying Set to true if the instruction modifies RSP.
 * @return Instruction length in bytes, or 0 if decoding fails.
 */
static usz xi_x86_insn_length(const u8* code, usz maxLen,
                               bool& outHasMem,
                               bool& outIsRelJmp,
                               usz& outRelOffset,
                               usz& outRelSize,
                               bool& outIsRspModifying,
                               usz& outImmSize) {
    outHasMem = false;
    outIsRelJmp = false;
    outRelOffset = 0;
    outRelSize = 0;
    outIsRspModifying = false;
    outImmSize = 0;

    if (maxLen == 0) return 0;

    usz pos = 0;
    bool hasRex = false;
    u8 rex = 0;
    bool rexW = false;
    bool has66 = false;
    bool has67 = false;

    // --- Legacy prefixes ---
    for (;;) {
        if (pos >= maxLen) return 0;
        u8 b = code[pos];
        if (b == 0x66) { has66 = true; ++pos; }
        else if (b == 0x67) { has67 = true; ++pos; }
        else if (b == 0xF0 || b == 0xF2 || b == 0xF3) { ++pos; } // LOCK, REPNE, REP
        else if (b == 0x2E || b == 0x36 || b == 0x3E ||
                 b == 0x26 || b == 0x64 || b == 0x65) { ++pos; } // Segment overrides
        else break;
    }

    // --- REX prefix ---
    if (pos < maxLen && (code[pos] & 0xF0) == 0x40) {
        hasRex = true;
        rex = code[pos];
        rexW = (rex & 0x08) != 0;
        ++pos;
    }
    (void)hasRex;

    if (pos >= maxLen) return 0;

    // --- Determine if this instruction has ModRM and what operands it needs ---
    bool hasModRM = false;
    usz immSize = 0; // Size of immediate operand in bytes.

    // --- Opcode ---
    u8 opcode = code[pos++];
    bool twoByteOpcode = false;

    if (opcode == 0x0F) {
        // Two-byte opcode escape.
        if (pos >= maxLen) return 0;
        opcode = code[pos++];
        twoByteOpcode = true;

        // 3-byte opcodes (0F 38 xx or 0F 3A xx)
        if (opcode == 0x38 || opcode == 0x3A) {
            if (pos >= maxLen) return 0;
            u8 opcode3 = code[pos++];
            (void)opcode3;
            // ModRM is always present, immediate if 3A
            hasModRM = true;
            immSize = (opcode == 0x3A) ? 1 : 0;
        }
    }

    // If already set by 3-byte opcode, skip switch
    if (twoByteOpcode && (opcode == 0x38 || opcode == 0x3A)) {
        // Already set
    } else if (twoByteOpcode) {
        // Most two-byte opcodes have ModRM. A few don't (e.g., 0F 05 = SYSCALL).
        // Simplified: check common no-ModRM two-byte opcodes.
        switch (opcode) {
            case 0x05: // SYSCALL
            case 0x06: // CLTS
            case 0x07: // SYSRET
            case 0x08: // INVD
            case 0x09: // WBINVD
            case 0x0B: // UD2
            case 0x31: // RDTSC
            case 0x34: // SYSENTER
            case 0x35: // SYSEXIT
            case 0xA2: // CPUID
            case 0x77: // EMMS
            case 0x30: // WRMSR
            case 0x32: // RDMSR
                hasModRM = false;
                break;

            // Jcc near (0F 80 - 0F 8F): relative 32-bit displacement, no ModRM.
            case 0x80: case 0x81: case 0x82: case 0x83:
            case 0x84: case 0x85: case 0x86: case 0x87:
            case 0x88: case 0x89: case 0x8A: case 0x8B:
            case 0x8C: case 0x8D: case 0x8E: case 0x8F:
                outIsRelJmp = true;
                outRelOffset = pos;
                outRelSize = 4;
                immSize = 4;
                hasModRM = false;
                break;

            default:
                hasModRM = true;
                break;
        }
        // push/pop FS/GS (0F A0, 0F A1, 0F A8, 0F A9)
        if (opcode == 0xA0 || opcode == 0xA1 || opcode == 0xA8 || opcode == 0xA9) {
            outIsRspModifying = true;
        }
    } else {
        // One-byte opcode analysis.
        switch (opcode) {
            // --- No operands ---
            case 0x90: // NOP
            case 0xC3: // RET
            case 0xCB: // RET far
            case 0xCC: // INT3
            case 0xF4: // HLT
            case 0xF5: // CMC
            case 0xF8: // CLC
            case 0xF9: // STC
            case 0xFA: // CLI
            case 0xFB: // STI
            case 0xFC: // CLD
            case 0xFD: // STD
            case 0x9C: // PUSHFQ
            case 0x9D: // POPFQ
            case 0x99: // CQO/CDQ
            case 0x98: // CWDE/CDQE
            case 0xC9: // LEAVE
            case 0xCE: // INTO
                hasModRM = false;
                immSize = 0;
                break;

            // --- RET imm16 ---
            case 0xC2:
            case 0xCA:
                hasModRM = false;
                immSize = 2;
                break;

            // --- Single-byte register push/pop (50-5F) ---
            case 0x50: case 0x51: case 0x52: case 0x53:
            case 0x54: case 0x55: case 0x56: case 0x57:
            case 0x58: case 0x59: case 0x5A: case 0x5B:
            case 0x5C: case 0x5D: case 0x5E: case 0x5F:
                hasModRM = false;
                immSize = 0;
                break;

            // --- XCHG rax,r (91-97) ---
            case 0x91: case 0x92: case 0x93: case 0x94:
            case 0x95: case 0x96: case 0x97:
                hasModRM = false;
                immSize = 0;
                break;

            // --- MOV imm to register (B0-B7: 8-bit, B8-BF: 32/64-bit) ---
            case 0xB0: case 0xB1: case 0xB2: case 0xB3:
            case 0xB4: case 0xB5: case 0xB6: case 0xB7:
                hasModRM = false;
                immSize = 1;
                break;
            case 0xB8: case 0xB9: case 0xBA: case 0xBB:
            case 0xBC: case 0xBD: case 0xBE: case 0xBF:
                hasModRM = false;
                immSize = rexW ? 8 : (has66 ? 2 : 4);
                break;

            // --- CALL rel32 ---
            case 0xE8:
                outIsRelJmp = true;
                outRelOffset = pos;
                outRelSize = 4;
                immSize = 4;
                hasModRM = false;
                break;

            // --- JMP rel32 ---
            case 0xE9:
                outIsRelJmp = true;
                outRelOffset = pos;
                outRelSize = 4;
                immSize = 4;
                hasModRM = false;
                break;

            // --- JMP rel8 ---
            case 0xEB:
                outIsRelJmp = true;
                outRelOffset = pos;
                outRelSize = 1;
                immSize = 1;
                hasModRM = false;
                break;

            // --- Jcc rel8 (70-7F) ---
            case 0x70: case 0x71: case 0x72: case 0x73:
            case 0x74: case 0x75: case 0x76: case 0x77:
            case 0x78: case 0x79: case 0x7A: case 0x7B:
            case 0x7C: case 0x7D: case 0x7E: case 0x7F:
                outIsRelJmp = true;
                outRelOffset = pos;
                outRelSize = 1;
                immSize = 1;
                hasModRM = false;
                break;

            // --- LOOP/LOOPE/LOOPNE/JECXZ (E0-E3) rel8 ---
            case 0xE0: case 0xE1: case 0xE2: case 0xE3:
                outIsRelJmp = true;
                outRelOffset = pos;
                outRelSize = 1;
                immSize = 1;
                hasModRM = false;
                break;

            // --- INT imm8 ---
            case 0xCD:
                hasModRM = false;
                immSize = 1;
                break;

            // --- IN/OUT imm8 ---
            case 0xE4: case 0xE5: case 0xE6: case 0xE7:
                hasModRM = false;
                immSize = 1;
                break;

            // --- IN/OUT DX ---
            case 0xEC: case 0xED: case 0xEE: case 0xEF:
                hasModRM = false;
                immSize = 0;
                break;

            // --- MOV AL/AX/EAX/RAX to/from moffs ---
            case 0xA0: case 0xA1: case 0xA2: case 0xA3:
                hasModRM = false;
                immSize = has67 ? 4 : 8; // Address size.
                outHasMem = true;
                break;

            // --- TEST AL/AX/EAX, imm ---
            case 0xA8:
                hasModRM = false;
                immSize = 1;
                break;
            case 0xA9:
                hasModRM = false;
                immSize = rexW ? 4 : (has66 ? 2 : 4); // Note: TEST rax,imm32 sign-extends.
                break;

            // --- ADD/OR/ADC/SBB/AND/SUB/XOR/CMP AL/AX, imm ---
            case 0x04: case 0x0C: case 0x14: case 0x1C:
            case 0x24: case 0x2C: case 0x34: case 0x3C:
                hasModRM = false;
                immSize = 1;
                break;
            case 0x05: case 0x0D: case 0x15: case 0x1D:
            case 0x25: case 0x2D: case 0x35: case 0x3D:
                hasModRM = false;
                immSize = has66 ? 2 : 4;
                break;

            // --- Opcodes with ModRM and possible immediate ---
            // 80-83: ALU r/m, imm
            case 0x80:
                hasModRM = true;
                immSize = 1;
                break;
            case 0x81:
                hasModRM = true;
                immSize = has66 ? 2 : 4;
                break;
            case 0x82: // Undocumented alias of 0x80 on some CPUs
                hasModRM = true;
                immSize = 1;
                break;
            case 0x83:
                hasModRM = true;
                immSize = 1;
                break;

            // C0/C1: shift r/m, imm8
            case 0xC0:
                hasModRM = true;
                immSize = 1;
                break;
            case 0xC1:
                hasModRM = true;
                immSize = 1;
                break;

            // C6: MOV r/m8, imm8
            case 0xC6:
                hasModRM = true;
                immSize = 1;
                break;

            // C7: MOV r/m16/32/64, imm16/32
            case 0xC7:
                hasModRM = true;
                immSize = has66 ? 2 : 4;
                break;

            // F6: TEST/NOT/NEG/MUL/IMUL/DIV/IDIV r/m8
            case 0xF6:
                hasModRM = true;
                if (pos < maxLen) {
                    u8 reg = (code[pos] >> 3) & 7;
                    immSize = (reg <= 1) ? 1 : 0;
                } else {
                    immSize = 0;
                }
                break;

            // F7: TEST/NOT/NEG/MUL/IMUL/DIV/IDIV r/m16/32/64
            case 0xF7:
                hasModRM = true;
                if (pos < maxLen) {
                    u8 reg = (code[pos] >> 3) & 7;
                    immSize = (reg <= 1) ? (has66 ? (usz)2 : (usz)4) : (usz)0;
                } else {
                    immSize = 0;
                }
                break;

            // 69: IMUL r, r/m, imm16/32
            case 0x69:
                hasModRM = true;
                immSize = has66 ? 2 : 4;
                break;

            // 6B: IMUL r, r/m, imm8
            case 0x6B:
                hasModRM = true;
                immSize = 1;
                break;

            // 6A: PUSH imm8
            case 0x6A:
                hasModRM = false;
                immSize = 1;
                break;

            // 68: PUSH imm16/32
            case 0x68:
                hasModRM = false;
                immSize = has66 ? 2 : 4;
                break;

            // 63: MOVSXD/MOVSLQ
            case 0x63:
                hasModRM = true;
                immSize = 0;
                break;

            default:
                if (opcode <= 0x3F) {
                    u8 lowBits = opcode & 0x07;
                    if (lowBits <= 3) {
                        hasModRM = true;
                        immSize = 0;
                    } else {
                        hasModRM = false;
                        immSize = 0;
                    }
                } else if ((opcode >= 0x84 && opcode <= 0x8F) ||
                           (opcode >= 0xD0 && opcode <= 0xD3) ||
                           (opcode >= 0xFE && opcode <= 0xFF) ||
                           opcode == 0x8D || // LEA
                           opcode == 0xD8 || opcode == 0xD9 || // x87
                           opcode == 0xDA || opcode == 0xDB ||
                           opcode == 0xDC || opcode == 0xDD ||
                           opcode == 0xDE || opcode == 0xDF) {
                    hasModRM = true;
                    immSize = 0;
                } else {
                    return 0;
                }
                break;
        }

        // RSP modifying one-byte opcodes
        if (opcode >= 0x50 && opcode <= 0x5F) { // push/pop reg
            outIsRspModifying = true;
        } else if (opcode == 0x6A || opcode == 0x68) { // push imm
            outIsRspModifying = true;
        } else if (opcode == 0xC9 || opcode == 0xC8) { // leave, enter
            outIsRspModifying = true;
        } else if (opcode == 0xE8) { // call rel32
            outIsRspModifying = true;
        } else if (opcode == 0xC3 || opcode == 0xC2 || opcode == 0xCB || opcode == 0xCA) { // ret
            outIsRspModifying = true;
        } else if (opcode == 0x94) { // xchg rax, rsp / xchg rsp, rax
            outIsRspModifying = true;
        } else if (opcode == 0x9C || opcode == 0x9D) { // pushfq / popfq
            outIsRspModifying = true;
        } else if (opcode == 0x8F) { // pop r/m64
            if (pos < maxLen) {
                u8 reg = (code[pos] >> 3) & 7;
                if (reg == 0) outIsRspModifying = true;
            }
        } else if (opcode == 0xFF) { // FF group: call indirect (reg == 2), push r/m64 (reg == 6)
            if (pos < maxLen) {
                u8 reg = (code[pos] >> 3) & 7;
                if (reg == 2 || reg == 6) outIsRspModifying = true;
            }
        }
    }

    // --- Decode ModRM ---
    if (hasModRM) {
        if (pos >= maxLen) return 0;
        u8 modrm = code[pos++];
        u8 mod = (modrm >> 6) & 3;
        u8 reg = (modrm >> 3) & 7;
        u8 rm  = modrm & 7;

        // Check if ModRM destination is RSP (register 4):
        bool rmIsRsp = (mod == 3 && rm == 4 && !(rex & 0x01));
        bool regIsRsp = (reg == 4 && !(rex & 0x04));

        if (rmIsRsp) {
            // Instructions that write to rm:
            if (opcode == 0x88 || opcode == 0x89 || opcode == 0xC6 || opcode == 0xC7 ||
                opcode == 0x80 || opcode == 0x81 || opcode == 0x83 ||
                opcode == 0x00 || opcode == 0x01 || opcode == 0x08 || opcode == 0x09 ||
                opcode == 0x10 || opcode == 0x11 || opcode == 0x18 || opcode == 0x19 ||
                opcode == 0x20 || opcode == 0x21 || opcode == 0x28 || opcode == 0x29 ||
                opcode == 0x30 || opcode == 0x31 ||
                opcode == 0xC0 || opcode == 0xC1 ||
                opcode == 0xD0 || opcode == 0xD1 || opcode == 0xD2 || opcode == 0xD3 ||
                opcode == 0xF6 || opcode == 0xF7 || opcode == 0xFF ||
                opcode == 0x86 || opcode == 0x87) {
                outIsRspModifying = true;
            }
        }
        if (regIsRsp) {
            // Instructions that write to reg:
            if (opcode == 0x8A || opcode == 0x8B || opcode == 0x8D ||
                opcode == 0x02 || opcode == 0x03 || opcode == 0x0A || opcode == 0x0B ||
                opcode == 0x12 || opcode == 0x13 || opcode == 0x1A || opcode == 0x1B ||
                opcode == 0x22 || opcode == 0x23 || opcode == 0x2A || opcode == 0x2B ||
                opcode == 0x32 || opcode == 0x33 ||
                opcode == 0x86 || opcode == 0x87) {
                outIsRspModifying = true;
            }
        }

        // Memory operand if mod != 3 (register direct) and not LEA, and not NOP/prefetch (0F 18 - 0F 1F).
        if (mod != 3 && !(opcode == 0x8D && !twoByteOpcode) && !(twoByteOpcode && opcode >= 0x18 && opcode <= 0x1F)) {
            outHasMem = true;
        }

        // Check for SIB byte (rm == 4 and mod != 3).
        if (mod != 3 && rm == 4) {
            if (pos >= maxLen) return 0;
            u8 sib = code[pos++];
            u8 base = sib & 7;

            // SIB with base == 5 and mod == 0 means disp32 with no base register.
            if (base == 5 && mod == 0) {
                pos += 4; // 32-bit displacement.
            }
        }

        // Displacement based on mod field.
        if (mod == 0) {
            // Special: rm == 5 means RIP-relative 32-bit displacement.
            if (rm == 5) {
                pos += 4;
            }
            // Otherwise no displacement (already handled SIB base==5 above).
        } else if (mod == 1) {
            pos += 1; // 8-bit displacement.
        } else if (mod == 2) {
            pos += 4; // 32-bit displacement.
        }
        // mod == 3: register direct, no displacement.
    }

    // --- Immediate ---
    pos += immSize;

    if (pos > maxLen) return 0;

    outImmSize = immSize;
    return pos;
}

// -------------------------------------------------------------------------
// SFI Stub Emission Helpers
// -------------------------------------------------------------------------

/**
 * @brief Size of the bounds-check call stub injected before memory ops.
 *
 * The stub is:
 *   push rax                     ; 1 byte   (save rax)
 *   push rdi                     ; 1 byte   (save rdi — used for arg)
 *   push rsi                     ; 1 byte   (save rsi — used for arg)
 *   movabs rax, <stub_addr>      ; 10 bytes (2 + 8)
 *   call rax                     ; 2 bytes  (FF D0)
 *   pop rsi                      ; 1 byte
 *   pop rdi                      ; 1 byte
 *   pop rax                      ; 1 byte
 *                           Total: 18 bytes
 */
static void parseLayout(const u8* code, usz len, usz& prefixLen, bool& hasRex, u8& rex, usz& opcodeLen, usz& modrmPos) {
    usz pos = 0;
    prefixLen = 0;
    hasRex = false;
    rex = 0;
    opcodeLen = 0;
    modrmPos = 0;
    
    // Skip legacy prefixes
    while (pos < len) {
        u8 b = code[pos];
        if (b == 0x66 || b == 0x67 || b == 0xF0 || b == 0xF2 || b == 0xF3 ||
            b == 0x2E || b == 0x36 || b == 0x3E || b == 0x26 || b == 0x64 || b == 0x65) {
            pos++;
        } else break;
    }
    prefixLen = pos;
    
    // Check REX prefix
    if (pos < len && (code[pos] & 0xF0) == 0x40) {
        hasRex = true;
        rex = code[pos];
        pos++;
    }
    
    // Opcode size
    if (pos < len) {
        u8 op = code[pos];
        if (op == 0x0F) {
            if (pos + 1 < len) {
                u8 op2 = code[pos + 1];
                if (op2 == 0x38 || op2 == 0x3A) {
                    opcodeLen = 3;
                } else {
                    opcodeLen = 2;
                }
            } else {
                opcodeLen = 1;
            }
        } else {
            opcodeLen = 1;
        }
    }
    pos += opcodeLen;
    modrmPos = pos;
}

static bool isStackRelativeAndSafe(const u8* code, usz len) {
    usz prefixLen = 0, modrmPos = 0, opcodeLen = 0;
    bool hasRex = false;
    u8 rex = 0;
    parseLayout(code, len, prefixLen, hasRex, rex, opcodeLen, modrmPos);
    if (modrmPos == 0 || modrmPos >= len) return false;

    u8 modrm = code[modrmPos];
    u8 mod = (modrm >> 6) & 3;
    u8 rm = modrm & 7;

    if (mod == 3) return false; // Register direct

    usz dispPos = 0;
    bool stackRelative = false;

    if (rm == 4) { // SIB present
        usz sibPos = modrmPos + 1;
        if (sibPos >= len) return false;
        u8 sib = code[sibPos];
        u8 base = sib & 7;
        u8 index = (sib >> 3) & 7;

        if (index != 4) return false; // Index register present

        if (base == 4) {
            stackRelative = true;
            dispPos = modrmPos + 2;
        } else if (base == 5 && mod != 0) {
            stackRelative = true;
            dispPos = modrmPos + 2;
        }
    } else {
        if (rm == 5 && mod != 0) {
            stackRelative = true;
            dispPos = modrmPos + 1;
        }
    }

    if (stackRelative) {
        if (mod == 0) {
            return true; // Offset is 0, always safe
        } else if (mod == 1) {
            // 8-bit displacement, offset is between -128 and 127
            return true; // Always safe for stack
        } else if (mod == 2) {
            // 32-bit displacement
            if (dispPos + 4 <= len) {
                i32 disp = 0;
                std::memcpy(&disp, code + dispPos, 4);
                // Safe if offset is within a reasonable range (e.g. +/- 4096)
                if (disp >= -4096 && disp <= 4096) {
                    return true;
                }
            }
        }
    }

    return false;
}

static usz getLeaSize(const u8* code, usz len, usz immSize) {
    usz prefixLen = 0, modrmPos = 0, opcodeLen = 0;
    bool hasRex = false;
    u8 rex = 0;
    parseLayout(code, len, prefixLen, hasRex, rex, opcodeLen, modrmPos);
    usz sibDispSize = (len - immSize) - (modrmPos + 1);
    return prefixLen + 3 + sibDispSize;
}

static usz buildLea(u8* out, const u8* insnSrc, usz instrLen, usz immSize) {
    usz pos = 0;
    usz outPos = 0;
    bool hasRex = false;
    u8 rex = 0;
    
    // Copy legacy prefixes
    for (;;) {
        u8 b = insnSrc[pos];
        if (b == 0x66 || b == 0x67 || b == 0xF0 || b == 0xF2 || b == 0xF3 ||
            b == 0x2E || b == 0x36 || b == 0x3E || b == 0x26 || b == 0x64 || b == 0x65) {
            out[outPos++] = b;
            pos++;
        } else break;
    }
    
    // Check original REX prefix
    if ((insnSrc[pos] & 0xF0) == 0x40) {
        hasRex = true;
        rex = insnSrc[pos];
        pos++;
    }
    
    // Emit new REX prefix targeting RDI (reg 7) with 64-bit operand size
    u8 newRex = 0x48; // REX.W
    if (hasRex) {
        newRex = (rex & ~0x04) | 0x48; // Clear REX.R, set REX.W
    }
    out[outPos++] = newRex;
    
    // Skip opcode
    if (insnSrc[pos] == 0x0F) {
        if (pos + 1 < instrLen && (insnSrc[pos + 1] == 0x38 || insnSrc[pos + 1] == 0x3A)) {
            pos += 3;
        } else {
            pos += 2;
        }
    } else {
        pos += 1;
    }
    
    // Emit LEA opcode (0x8D)
    out[outPos++] = 0x8D;
    
    // Modify ModRM to target RDI (reg = 7)
    u8 modrm = insnSrc[pos++];
    u8 newModrm = (modrm & 0xC7) | 0x38; // 7 << 3 = 0x38
    out[outPos++] = newModrm;
    
    // Copy the remaining bytes (SIB, displacement) except immediate
    usz rem = instrLen - pos - immSize;
    if (rem > 0) {
        std::memcpy(out + outPos, insnSrc + pos, rem);
        outPos += rem;
    }
    
    return outPos;
}

static usz getMemAccessSize(const u8* code, usz len, bool rexW, bool has66) {
    usz prefixLen = 0, modrmPos = 0, opcodeLen = 0;
    bool hasRex = false;
    u8 rex = 0;
    parseLayout(code, len, prefixLen, hasRex, rex, opcodeLen, modrmPos);
    
    u8 opcode = code[prefixLen + (hasRex ? 1 : 0)];
    if (opcode == 0x0F) {
        opcode = code[prefixLen + (hasRex ? 1 : 0) + 1];
    }
    
    if (opcode == 0xFF || opcode == 0x8F) {
        return 8;
    }
    
    if ((opcode & 1) == 0) {
        return 1;
    }
    
    if (rexW) return 8;
    if (has66) return 2;
    return 4;
}

static void emitBoundsCheckStub(u8*& out, u64 stubAddr, const u8* insnSrc, usz instrLen, usz immSize, usz accessSize) {
    // 1. Save all caller-saved registers and RFLAGS (10 registers = 80 bytes)
    *out++ = 0x9C; // pushfq
    *out++ = 0x50; // push rax
    *out++ = 0x51; // push rcx
    *out++ = 0x52; // push rdx
    *out++ = 0x56; // push rsi
    *out++ = 0x57; // push rdi
    *out++ = 0x41; *out++ = 0x50; // push r8
    *out++ = 0x41; *out++ = 0x51; // push r9
    *out++ = 0x41; *out++ = 0x52; // push r10
    *out++ = 0x41; *out++ = 0x53; // push r11

    // 2. Temporarily restore rsp so buildLea computes the correct address
    // add rsp, 80 -> 48 83 C4 50
    *out++ = 0x48; *out++ = 0x83; *out++ = 0xC4; *out++ = 0x50;

    // 3. Construct and emit the LEA instruction to compute the target address into rdi
    usz leaLen = buildLea(out, insnSrc, instrLen, immSize);
    out += leaLen;

    // 4. Restore the shifted rsp
    // sub rsp, 80 -> 48 83 EC 50
    *out++ = 0x48; *out++ = 0x83; *out++ = 0xEC; *out++ = 0x50;

    // 5. Pass accessSize in rsi
    // push accessSize (imm8)
    *out++ = 0x6A;
    *out++ = static_cast<u8>(accessSize);
    // pop rsi
    *out++ = 0x5E;

    // 6. Dynamically align the stack to 16 bytes before calling C++ helper
    // push rbp -> 55
    *out++ = 0x55;
    // mov rbp, rsp -> 48 89 E5
    *out++ = 0x48; *out++ = 0x89; *out++ = 0xE5;
    // and rsp, -16 -> 48 83 E4 F0
    *out++ = 0x48; *out++ = 0x83; *out++ = 0xE4; *out++ = 0xF0;

    // 7. Call the bounds check helper
    // movabs rax, stubAddr
    *out++ = 0x48;
    *out++ = 0xB8;
    std::memcpy(out, &stubAddr, 8);
    out += 8;

    // call rax (FF D0)
    *out++ = 0xFF;
    *out++ = 0xD0;

    // 8. Restore stack and unaligned rsp
    // leave -> C9 (mov rsp, rbp; pop rbp)
    *out++ = 0xC9;

    // 9. Restore caller-saved registers
    *out++ = 0x41; *out++ = 0x5B; // pop r11
    *out++ = 0x41; *out++ = 0x5A; // pop r10
    *out++ = 0x41; *out++ = 0x59; // pop r9
    *out++ = 0x41; *out++ = 0x58; // pop r8
    *out++ = 0x5F; // pop rdi
    *out++ = 0x5E; // pop rsi
    *out++ = 0x5A; // pop rdx
    *out++ = 0x59; // pop rcx
    *out++ = 0x58; // pop rax
    *out++ = 0x9D; // popfq
}

/**
 * @brief Size of the indirect-jump validation stub.
 *
 * For indirect JMP/CALL (FF /2, FF /4), we replace them with:
 *   push rax                          ; 1 byte
 *   push rdi                          ; 1 byte
 *   <original instruction but target to rdi instead>
 *   movabs rax, <xi_sfi_jump_check>   ; 10 bytes
 *   call rax                          ; 2 bytes
 *   pop rdi                           ; 1 byte
 *   pop rax                           ; 1 byte
 *   <original instruction>            ; N bytes
 *                                Total: 16 + N bytes
 *
 * Actually, a simpler approach: we emit a stub that:
 *   1. Saves registers
 *   2. Calls xi_sfi_jump_check with the computed target
 *   3. Restores registers
 *   4. Executes the original indirect jump
 *
 * Since the jump target is computed from a register or memory, we can't
 * easily extract it before the instruction executes. Instead, we use
 * a different strategy: convert the indirect jump to a call to our
 * validator, which inspects the target and either allows or traps.
 *
 * Simplest approach for correctness: ban all indirect jumps/calls
 * in AOT'd code. Tasks must use direct (relative) jumps only.
 * If a task needs dynamic dispatch, it must go through a registered
 * callback mechanism.
 *
 * BUT: the compiler generates indirect calls for virtual functions,
 * function pointers, etc. So we need a runtime check approach.
 *
 * We use: replace the indirect jmp/call with a sequence that first
 * validates, then executes. For register indirect (e.g., call rax):
 *
 *   push rdi           ; 1 byte — save rdi
 *   mov rdi, rax       ; 3 bytes — target to rdi (arg for jump check)
 *   push rax           ; 1 byte — save rax (our target)
 *   movabs rax, <check>; 10 bytes
 *   call rax           ; 2 bytes — validate (traps if invalid)
 *   pop rax            ; 1 byte — restore target
 *   pop rdi            ; 1 byte — restore rdi
 *   call rax           ; 2 bytes — execute the original indirect call
 *                  Total: 21 bytes for register-indirect
 *
 * For memory-indirect (e.g., call [rax+8]), it's more complex.
 * For simplicity and MAXIMUM SAFETY, we replace ALL indirect
 * JMP/CALL with ud2 unless the task has explicitly allowed them.
 * This is the strictest possible policy — zero indirect jumps.
 *
 * However, the trampoline system and task entry use indirect calls,
 * so we only ban them INSIDE AOT'd code (task code), not in kernel code.
 */
static constexpr usz kJumpCheckStubSize = 2; // ud2 for banned indirect jumps

/**
 * @brief Size of the RET validation stub.
 *
 * For RET, we replace with a sequence that avoids clobbering RDI:
 *   push rax                          ; 1 byte
 *   mov rax, [rsp+8]                  ; 5 bytes — read return address
 *   push rdi                          ; 1 byte — save original rdi
 *   mov rdi, rax                      ; 3 bytes — target to rdi
 *   movabs rax, <xi_sfi_jump_check>   ; 10 bytes
 *   call rax                          ; 2 bytes — validate
 *   pop rdi                           ; 1 byte — restore rdi
 *   pop rax                           ; 1 byte — restore rax
 *   ret                               ; 1 byte — execute validated ret
 *                                Total: 25 bytes
 */
static constexpr usz kRetCheckStubSize = 66;

static void emitRetCheckStub(u8*& out, u64 resolverAddr) {
    // 1. Save original rdi and rax to compute safely
    *out++ = 0x57; // push rdi
    *out++ = 0x50; // push rax
    
    // 2. Read the return address (which is now at rsp + 16) into rdi
    // mov rdi, [rsp+16] (48 8b 7c 24 10)
    *out++ = 0x48; *out++ = 0x8B; *out++ = 0x7C; *out++ = 0x24; *out++ = 0x10;
    
    // 3. Save all other caller-saved registers and RFLAGS (8 items = 64 bytes)
    *out++ = 0x9C; // pushfq
    *out++ = 0x51; // push rcx
    *out++ = 0x52; // push rdx
    *out++ = 0x56; // push rsi
    *out++ = 0x41; *out++ = 0x50; // push r8
    *out++ = 0x41; *out++ = 0x51; // push r9
    *out++ = 0x41; *out++ = 0x52; // push r10
    *out++ = 0x41; *out++ = 0x53; // push r11
    
    // 4. Align the stack dynamically
    *out++ = 0x55; // push rbp
    *out++ = 0x48; *out++ = 0x89; *out++ = 0xE5; // mov rbp, rsp
    *out++ = 0x48; *out++ = 0x83; *out++ = 0xE4; *out++ = 0xF0; // and rsp, -16
    
    // 5. Call the resolver helper
    // movabs rax, resolverAddr
    *out++ = 0x48; *out++ = 0xB8;
    std::memcpy(out, &resolverAddr, 8);
    out += 8;
    // call rax
    *out++ = 0xFF; *out++ = 0xD0;
    
    // 6. Restore stack
    *out++ = 0xC9; // leave
    
    // 7. Restore all registers (save rax into r11 first!)
    // mov r11, rax (49 89 C3)
    *out++ = 0x49; *out++ = 0x89; *out++ = 0xC3;
    
    // add rsp, 8 (skip popped r11)
    *out++ = 0x48; *out++ = 0x83; *out++ = 0xC4; *out++ = 0x08;
    
    *out++ = 0x41; *out++ = 0x5A; // pop r10
    *out++ = 0x41; *out++ = 0x59; // pop r9
    *out++ = 0x41; *out++ = 0x58; // pop r8
    *out++ = 0x5E; // pop rsi
    *out++ = 0x5A; // pop rdx
    *out++ = 0x59; // pop rcx
    *out++ = 0x9D; // popfq
    *out++ = 0x58; // pop rax
    *out++ = 0x5F; // pop rdi
    
    // 8. Pop the return address off the stack (advance rsp by 8)
    // add rsp, 8 (48 83 C4 08)
    *out++ = 0x48; *out++ = 0x83; *out++ = 0xC4; *out++ = 0x08;
    
    // 9. Jump to r11 (41 FF E3)
    *out++ = 0x41; *out++ = 0xFF; *out++ = 0xE3;
}

static constexpr usz kIndirectJumpCheckStubSize = 58;

static void emitIndirectJumpStub(u8*& out, const u8* insnSrc, usz instrLen, u64 resolverAddr) {
    // 1. Save original rdi and rax to compute safely
    *out++ = 0x57; // push rdi
    *out++ = 0x50; // push rax
    
    // 2. Execute the modified instruction (e.g. push [rbx]).
    // This pushes the TARGET address onto the stack.
    usz opcodePos = 0;
    while (opcodePos < instrLen) {
        u8 b = insnSrc[opcodePos];
        if (b == 0x66 || b == 0x67 || b == 0xF0 || b == 0xF2 || b == 0xF3 ||
            b == 0x2E || b == 0x36 || b == 0x3E || b == 0x26 || b == 0x64 || b == 0x65) {
            ++opcodePos;
        } else break;
    }
    if (opcodePos < instrLen && (insnSrc[opcodePos] & 0xF0) == 0x40) {
        ++opcodePos;
    }
    std::memcpy(out, insnSrc, instrLen);
    u8 modrm = insnSrc[opcodePos + 1];
    out[opcodePos + 1] = (modrm & 0xC7) | 0x30; // reg = 6 (push)
    out += instrLen;
    
    // 3. Pop the target into rdi
    *out++ = 0x5F; // pop rdi
    
    // 4. Save other caller-saved registers and RFLAGS (8 items = 64 bytes)
    *out++ = 0x9C; // pushfq
    *out++ = 0x51; // push rcx
    *out++ = 0x52; // push rdx
    *out++ = 0x56; // push rsi
    *out++ = 0x41; *out++ = 0x50; // push r8
    *out++ = 0x41; *out++ = 0x51; // push r9
    *out++ = 0x41; *out++ = 0x52; // push r10
    *out++ = 0x41; *out++ = 0x53; // push r11
    
    // 5. Align stack dynamically
    *out++ = 0x55; // push rbp
    *out++ = 0x48; *out++ = 0x89; *out++ = 0xE5; // mov rbp, rsp
    *out++ = 0x48; *out++ = 0x83; *out++ = 0xE4; *out++ = 0xF0; // and rsp, -16
    
    // 6. Call the jump check helper
    // movabs rax, resolverAddr
    *out++ = 0x48; *out++ = 0xB8;
    std::memcpy(out, &resolverAddr, 8);
    out += 8;
    // call rax
    *out++ = 0xFF; *out++ = 0xD0;
    
    // 7. Restore stack
    *out++ = 0xC9; // leave
    
    // 8. Restore registers (save rax into r11 first!)
    // mov r11, rax (49 89 C3)
    *out++ = 0x49; *out++ = 0x89; *out++ = 0xC3;
    
    // pop the rest
    // Actually, pop r11 would overwrite the resolved target!
    // So we add 8 to rsp instead of popping r11!
    // add rsp, 8 (48 83 C4 08)
    *out++ = 0x48; *out++ = 0x83; *out++ = 0xC4; *out++ = 0x08;
    
    *out++ = 0x41; *out++ = 0x5A; // pop r10
    *out++ = 0x41; *out++ = 0x59; // pop r9
    *out++ = 0x41; *out++ = 0x58; // pop r8
    *out++ = 0x5E; // pop rsi
    *out++ = 0x5A; // pop rdx
    *out++ = 0x59; // pop rcx
    *out++ = 0x9D; // popfq
    *out++ = 0x58; // pop rax
    *out++ = 0x5F; // pop rdi
    
    // 9. Execute the jump/call using r11
    u8 reg = (modrm >> 3) & 7;
    bool isCall = (reg == 2 || reg == 3);
    if (isCall) {
        // call r11 (41 FF D3)
        *out++ = 0x41; *out++ = 0xFF; *out++ = 0xD3;
    } else {
        // jmp r11 (41 FF E3)
        *out++ = 0x41; *out++ = 0xFF; *out++ = 0xE3;
    }
}

static constexpr usz kDirectCrossPageStubSize = 67;

static void emitDirectCrossPageStub(u8*& out, u64 targetGuestVirtualAddr, u64 resolverAddr, bool isCall) {
    u8* start = out;
    // 1. Save original rdi and rax to compute safely
    *out++ = 0x57; // push rdi
    *out++ = 0x50; // push rax

    // 2. Load targetGuestVirtualAddr into rdi
    *out++ = 0x48; *out++ = 0xBF; // movabs rdi, targetGuestVirtualAddr
    std::memcpy(out, &targetGuestVirtualAddr, 8);
    out += 8;

    // 3. Save other caller-saved registers and RFLAGS (8 items = 64 bytes)
    *out++ = 0x9C; // pushfq
    *out++ = 0x51; // push rcx
    *out++ = 0x52; // push rdx
    *out++ = 0x56; // push rsi
    *out++ = 0x41; *out++ = 0x50; // push r8
    *out++ = 0x41; *out++ = 0x51; // push r9
    *out++ = 0x41; *out++ = 0x52; // push r10
    *out++ = 0x41; *out++ = 0x53; // push r11

    // 4. Align stack dynamically
    *out++ = 0x55; // push rbp
    *out++ = 0x48; *out++ = 0x89; *out++ = 0xE5; // mov rbp, rsp
    *out++ = 0x48; *out++ = 0x83; *out++ = 0xE4; *out++ = 0xF0; // and rsp, -16

    // 5. Call resolverAddr
    *out++ = 0x48; *out++ = 0xB8; // movabs rax, resolverAddr
    std::memcpy(out, &resolverAddr, 8);
    out += 8;
    *out++ = 0xFF; *out++ = 0xD0; // call rax

    // 6. Restore stack
    *out++ = 0xC9; // leave

    // 7. Restore registers (save rax into r11 first!)
    *out++ = 0x49; *out++ = 0x89; *out++ = 0xC3; // mov r11, rax
    *out++ = 0x48; *out++ = 0x83; *out++ = 0xC4; *out++ = 0x08; // add rsp, 8 (skip popped r11)
    *out++ = 0x41; *out++ = 0x5A; // pop r10
    *out++ = 0x41; *out++ = 0x59; // pop r9
    *out++ = 0x41; *out++ = 0x58; // pop r8
    *out++ = 0x5E; // pop rsi
    *out++ = 0x5A; // pop rdx
    *out++ = 0x59; // pop rcx
    *out++ = 0x9D; // popfq
    *out++ = 0x58; // pop rax
    *out++ = 0x5F; // pop rdi

    // 8. Execute call or jump
    if (isCall) {
        *out++ = 0x41; *out++ = 0xFF; *out++ = 0xD3; // call r11
    } else {
        *out++ = 0x41; *out++ = 0xFF; *out++ = 0xE3; // jmp r11
    }

    ::printf("[emitDirectCrossPageStub] Generated stub for target=0x%lx (isCall=%d) at %p:\n  ",
             (unsigned long)targetGuestVirtualAddr, (int)isCall, start);
    for (u8* p = start; p < out; ++p) {
        ::printf("%02x ", *p);
    }
    ::printf("\n");
    ::fflush(stdout);
}

static bool isTargetInPage(usz pos, usz len, usz relOffset, usz relSize, const u8* code, usz codeSize) {
    i64 origDisp = 0;
    if (relSize == 4) {
        i32 disp32 = 0;
        std::memcpy(&disp32, code + pos + relOffset, 4);
        origDisp = disp32;
    } else if (relSize == 1) {
        i8 disp8 = static_cast<i8>(code[pos + relOffset]);
        origDisp = disp8;
    }
    i64 origTarget = static_cast<i64>(pos) + static_cast<i64>(len) + origDisp;
    return origTarget >= 0 && origTarget < static_cast<i64>(codeSize);
}


/**
 * @brief Size of the stack-check stub.
 *
 * Emitted after RSP-modifying instructions:
 *   push rax                          ; 1 byte
 *   push rdi                          ; 1 byte
 *   mov rdi, rsp                      ; 3 bytes (48 89 E7) — pass RSP
 *   movabs rax, <xi_sfi_stack_check>   ; 10 bytes
 *   call rax                          ; 2 bytes — validate
 *   pop rdi                           ; 1 byte
 *   pop rax                           ; 1 byte
 *                                Total: 54 bytes
 */
static constexpr usz kStackCheckStubSize = 54;

static void emitStackCheckStub(u8*& out, u64 stackCheckAddr) {
    // 1. Save all caller-saved registers and RFLAGS (80 bytes of pushes)
    *out++ = 0x9C; // pushfq
    *out++ = 0x50; // push rax
    *out++ = 0x51; // push rcx
    *out++ = 0x52; // push rdx
    *out++ = 0x56; // push rsi
    *out++ = 0x57; // push rdi
    *out++ = 0x41; *out++ = 0x50; // push r8
    *out++ = 0x41; *out++ = 0x51; // push r9
    *out++ = 0x41; *out++ = 0x52; // push r10
    *out++ = 0x41; *out++ = 0x53; // push r11

    // 2. Compute the original RSP into rdi: lea rdi, [rsp + 80]
    *out++ = 0x48; *out++ = 0x8D; *out++ = 0x7C; *out++ = 0x24; *out++ = 0x50;

    // 3. Align stack dynamically before call
    *out++ = 0x55; // push rbp
    *out++ = 0x48; *out++ = 0x89; *out++ = 0xE5; // mov rbp, rsp
    *out++ = 0x48; *out++ = 0x83; *out++ = 0xE4; *out++ = 0xF0; // and rsp, -16

    // 4. Call helper
    *out++ = 0x48; *out++ = 0xB8; // movabs rax, imm64
    std::memcpy(out, &stackCheckAddr, 8);
    out += 8;
    *out++ = 0xFF; *out++ = 0xD0; // call rax

    // 5. Restore stack
    *out++ = 0xC9; // leave

    // 6. Restore caller-saved registers
    *out++ = 0x41; *out++ = 0x5B; // pop r11
    *out++ = 0x41; *out++ = 0x5A; // pop r10
    *out++ = 0x41; *out++ = 0x59; // pop r9
    *out++ = 0x41; *out++ = 0x58; // pop r8
    *out++ = 0x5F; // pop rdi
    *out++ = 0x5E; // pop rsi
    *out++ = 0x5A; // pop rdx
    *out++ = 0x59; // pop rcx
    *out++ = 0x58; // pop rax
    *out++ = 0x9D; // popfq
}

/**
 * @brief Detects if an instruction writes to memory (store).
 */
static bool isStoreInstruction(const u8* code, usz len) {
    if (len == 0) return false;

    usz pos = 0;
    for (;;) {
        if (pos >= len) return false;
        u8 b = code[pos];
        if (b == 0x66 || b == 0x67 || b == 0xF0 || b == 0xF2 || b == 0xF3 ||
            b == 0x2E || b == 0x36 || b == 0x3E || b == 0x26 || b == 0x64 || b == 0x65) {
            ++pos;
        } else break;
    }
    if (pos < len && (code[pos] & 0xF0) == 0x40) {
        ++pos;
    }
    if (pos >= len) return false;

    u8 op = code[pos];

    if (op == 0x88 || op == 0x89) return true;
    if (op == 0xC6 || op == 0xC7) return true;
    if (op == 0x8F) return true;

    if (op == 0x00 || op == 0x01 ||
        op == 0x08 || op == 0x09 ||
        op == 0x10 || op == 0x11 ||
        op == 0x18 || op == 0x19 ||
        op == 0x20 || op == 0x21 ||
        op == 0x28 || op == 0x29 ||
        op == 0x30 || op == 0x31) {
        return true;
    }

    if ((op == 0x80 || op == 0x81 || op == 0x83) && pos + 1 < len) {
        u8 modrm = code[pos + 1];
        u8 reg = (modrm >> 3) & 7;
        if (reg != 7) return true;
    }

    if (op == 0xC0 || op == 0xC1 || op == 0xD0 || op == 0xD1 || op == 0xD2 || op == 0xD3) {
        return true;
    }

    if (op == 0xFF && pos + 1 < len) {
        u8 modrm = code[pos + 1];
        u8 reg = (modrm >> 3) & 7;
        if (reg == 0 || reg == 1) return true;
    }

    if ((op == 0xF6 || op == 0xF7) && pos + 1 < len) {
        u8 modrm = code[pos + 1];
        u8 reg = (modrm >> 3) & 7;
        if (reg == 2 || reg == 3) return true;
    }

    return false;
}

struct MemOperand {
    bool valid = false;
    int baseReg = -1;
    int indexReg = -1;
    int scale = 0;
    i64 disp = 0;
};

static void parseMemOperand(const u8* code, usz len, usz immSize, MemOperand& op) {
    usz pos = 0;
    for (;;) {
        u8 b = code[pos];
        if (b == 0x66 || b == 0x67 || b == 0xF0 || b == 0xF2 || b == 0xF3 ||
            b == 0x2E || b == 0x36 || b == 0x3E || b == 0x26 || b == 0x64 || b == 0x65) {
            pos++;
        } else break;
    }
    
    bool hasRex = false;
    u8 rex = 0;
    if ((code[pos] & 0xF0) == 0x40) {
        hasRex = true;
        rex = code[pos];
        pos++;
    }
    
    u8 opcode = code[pos];
    bool twoByteOpcode = false;
    if (opcode == 0x0F) {
        if (pos + 1 < len && (code[pos + 1] == 0x38 || code[pos + 1] == 0x3A)) {
            pos += 3;
        } else {
            pos += 2;
        }
        twoByteOpcode = true;
    } else {
        pos += 1;
    }
    
    u8 modrm = code[pos++];
    u8 mod = (modrm >> 6) & 3;
    u8 reg = (modrm >> 3) & 7;
    u8 rm  = modrm & 7;
    
    if (mod == 3) {
        return;
    }
    
    bool hasSib = (rm == 4);
    u8 base = 0;
    u8 sibBase = 0;
    bool hasBase = true;
    
    if (hasSib) {
        u8 sib = code[pos++];
        u8 sibScale = (sib >> 6) & 3;
        u8 sibIndex = (sib >> 3) & 7;
        sibBase = sib & 7;
        
        op.scale = 1 << sibScale;
        
        if (sibIndex != 4 || (hasRex && (rex & 0x02))) {
            op.indexReg = sibIndex | ((rex & 0x02) ? 8 : 0);
        }
        
        if (sibBase == 5 && mod == 0) {
            hasBase = false;
        } else {
            base = sibBase;
        }
    } else {
        if (mod == 0 && rm == 5) {
            hasBase = false;
        } else {
            base = rm;
        }
    }
    
    if (hasBase) {
        op.baseReg = base | ((rex & 0x01) ? 8 : 0);
    }
    
    i64 displacement = 0;
    if (hasSib && sibBase == 5 && mod == 0) {
        i32 d32 = 0;
        std::memcpy(&d32, code + pos, 4);
        displacement = d32;
    } else if (mod == 0 && rm == 5) {
        i32 d32 = 0;
        std::memcpy(&d32, code + pos, 4);
        displacement = d32;
    } else if (mod == 1) {
        i8 d8 = static_cast<i8>(code[pos]);
        displacement = d8;
    } else if (mod == 2) {
        i32 d32 = 0;
        std::memcpy(&d32, code + pos, 4);
        displacement = d32;
    }
    
    op.disp = displacement;
    op.valid = true;
}

static bool modifiesRbp(const u8* code, usz len, bool hasModRM, u8 modrm, u8 opcode, bool twoByteOpcode, bool hasRex, u8 rex) {
    if (len == 0) return false;
    
    usz pos = 0;
    for (;;) {
        u8 b = code[pos];
        if (b == 0x66 || b == 0x67 || b == 0xF0 || b == 0xF2 || b == 0xF3 ||
            b == 0x2E || b == 0x36 || b == 0x3E || b == 0x26 || b == 0x64 || b == 0x65) {
            pos++;
        } else break;
    }
    if (hasRex) pos++;
    if (twoByteOpcode) pos++;
    u8 op = code[pos];
    
    if (op == 0x55 || op == 0x5D) {
        return true;
    }
    
    if (hasModRM) {
        u8 mod = (modrm >> 6) & 3;
        u8 reg = (modrm >> 3) & 7;
        u8 rm  = modrm & 7;
        bool rmIsRbp = (mod == 3 && rm == 5 && !(rex & 0x01));
        bool regIsRbp = (reg == 5 && !(rex & 0x04));
        
        if (rmIsRbp) {
            if (op == 0x88 || op == 0x89 || op == 0xC6 || op == 0xC7 ||
                op == 0x80 || op == 0x81 || op == 0x83 ||
                op == 0x00 || op == 0x01 || op == 0x08 || op == 0x09 ||
                op == 0x10 || op == 0x11 || op == 0x18 || op == 0x19 ||
                op == 0x20 || op == 0x21 || op == 0x28 || op == 0x29 ||
                op == 0x30 || op == 0x31 ||
                op == 0xC0 || op == 0xC1 ||
                op == 0xD0 || op == 0xD1 || op == 0xD2 || op == 0xD3 ||
                op == 0xF6 || op == 0xF7 || op == 0xFF ||
                op == 0x86 || op == 0x87) {
                return true;
            }
        }
        if (regIsRbp) {
            if (op == 0x8A || op == 0x8B || op == 0x8D ||
                op == 0x02 || op == 0x03 || op == 0x0A || op == 0x0B ||
                op == 0x12 || op == 0x13 || op == 0x1A || op == 0x1B ||
                op == 0x22 || op == 0x23 || op == 0x2A || op == 0x2B ||
                op == 0x32 || op == 0x33 ||
                op == 0x86 || op == 0x87) {
                return true;
            }
        }
    }
    return false;
}

// -------------------------------------------------------------------------
// AOT::rewrite — Main Rewriter
// -------------------------------------------------------------------------

AOTResult AOT::rewrite(const u8* code, usz codeSize,
                       const Array<MemoryRegion>& regions,
                       usz taskBase) {
    AOTResult result;
    result.patchedCode = nullptr;
    result.patchedSize = 0;
    result.originalSize = codeSize;
    result.success = false;

    if (code == nullptr || codeSize == 0) {
        result.success = true;
        return result;
    }

    usz outputSize = 0;
    usz pos = 0;

    struct InsnInfo {
        usz origOffset;    // Offset in original code.
        usz instrLen;      // Length of the original instruction.
        usz outputOffset;  // Offset in the output code.
        bool hasMem;       // Whether a bounds-check stub was inserted.
        bool isRelJmp;     // Whether this is a relative jump/call.
        usz relOffset;     // Offset of relative displacement within instruction.
        usz relSize;       // Size of relative displacement (1 or 4).
        bool isHooked;     // Whether the instruction is hooked or banned.
        bool banned;       // Whether the instruction is banned.
        void* callbackPtr; // Pointer to registered callback (stored in TaskState).
        bool isBannedPrivileged; // Force trap (fails decoding).
        bool isIndirectJump;     // Indirect JMP/CALL — banned for containment.
        bool isRet;              // RET — rewritten with validation.
        bool isRspModifying;     // RSP modifying instruction.
        usz immSize;
        usz leaSize;
        usz accessSize;
        bool isTranslate;
        void* translateCallbackPtr;
    };

    struct RewritingGuard {
        RewritingGuard(usz physical) {
            tl_currently_rewriting_physical = physical;
        }
        ~RewritingGuard() {
            tl_currently_rewriting_physical = 0;
        }
    };
    RewritingGuard guard(reinterpret_cast<usz>(code));

    usz insnCapacity = codeSize;
    InsnInfo* insns = static_cast<InsnInfo*>(std::malloc(insnCapacity * sizeof(InsnInfo)));
    if (!insns) {
        return result;
    }

    usz insnCount = 0;

    while (pos < codeSize) {
        bool hasMem = false;
        bool isRelJmp = false;
        usz relOffset = 0;
        usz relSize = 0;
        bool isRspModifying = false;
        usz immSize = 0;

        usz maxLen = codeSize - pos;
        if (maxLen < 15) {
            TaskState* state = xi_get_current_task();
            if (state) {
                usz guestBase = 0;
                for (usz r = 0; r < state->regions.size(); ++r) {
                    if (state->regions[r].physical == code) {
                        guestBase = state->regions[r].base;
                        break;
                    }
                }
                if (guestBase != 0) {
                    usz nextPageAddr = guestBase + codeSize;
                    bool nextMapped = false;
                    for (usz r = 0; r < state->regions.size(); ++r) {
                        if (nextPageAddr >= state->regions[r].base && 
                            nextPageAddr < state->regions[r].base + state->regions[r].size) {
                            nextMapped = true;
                            break;
                        }
                    }
                    if (!nextMapped) {
                        for (usz f = 0; f < state->fetchRanges.size(); ++f) {
                            TaskState::FetchRange fr = state->fetchRanges[f];
                            if (nextPageAddr >= fr.start && nextPageAddr < fr.end) {
                                TaskState* prev = xi_get_current_task();
                                xi_set_current_task(nullptr);
                                fr.callback(fr.start, fr.end);
                                xi_set_current_task(prev);
                                nextMapped = true;
                                break;
                            }
                        }
                    }
                    if (nextMapped) {
                        maxLen = 15;
                    }
                }
            }
        }

        usz len = xi_x86_insn_length(code + pos, maxLen,
                                      hasMem, isRelJmp, relOffset, relSize, isRspModifying, immSize);

        if (hasMem && isStackRelativeAndSafe(code + pos, len)) {
            hasMem = false;
        }

        if (len == 0) {
            // Decoding failed — unknown instruction. Trap it.
            len = 1;
            hasMem = false;
            isRelJmp = false;
        }

        String instrName = getInstructionName(code + pos, len);
        InstructionMatch match = checkInstructionHooks(code + pos, len, instrName);

        bool isHooked = match.matched;
        bool hookBanned = match.banned;
        void* callbackPtr = match.callbackPtr;
        bool isTranslate = match.isTranslate;
        void* translateCallbackPtr = match.translateCallbackPtr;

        bool isBannedPrivileged = false;
        if (len == 1 && pos + 1 <= codeSize) {
            bool dummy1, dummy2, dummy5;
            usz dummy3, dummy4, dummy6;
            usz recheck = xi_x86_insn_length(code + pos, codeSize - pos,
                                             dummy1, dummy2, dummy3, dummy4, dummy5, dummy6);
            if (recheck == 0) {
                isBannedPrivileged = true; // Force trap for undecoded instruction.
            }
        }

        bool isIndirectJump = false;
        bool isRet = false;
        detectIndirectControl(code + pos, len, isIndirectJump, isRet);

        InsnInfo& info = insns[insnCount];
        info.origOffset = pos;
        info.instrLen = len;
        info.outputOffset = outputSize;
        info.hasMem = hasMem;
        info.isRelJmp = isRelJmp;
        info.relOffset = relOffset;

        info.relSize = relSize;
        info.isHooked = isHooked;
        info.banned = hookBanned;
        info.callbackPtr = callbackPtr;
        info.isBannedPrivileged = isBannedPrivileged;
        info.isIndirectJump = isIndirectJump;
        info.isRet = isRet;
        info.isRspModifying = isRspModifying;
        info.immSize = immSize;
        info.isTranslate = isTranslate;
        info.translateCallbackPtr = translateCallbackPtr;

        if (hasMem) {
            usz prefixLen = 0, modrmPos = 0, opcodeLen = 0;
            bool hasRex = false;
            u8 rex = 0;
            parseLayout(code + pos, len, prefixLen, hasRex, rex, opcodeLen, modrmPos);
            bool rexW = hasRex && (rex & 0x08);
            bool has66 = false;
            for (usz p = 0; p < prefixLen; ++p) {
                if (code[pos + p] == 0x66) {
                    has66 = true;
                    break;
                }
            }
            info.leaSize = getLeaSize(code + pos, len, immSize);
            info.accessSize = getMemAccessSize(code + pos, len, rexW, has66);
        } else {
            info.leaSize = 0;
            info.accessSize = 0;
        }

        usz insnOutputSize = 0;
        if (isBannedPrivileged) {
            insnOutputSize = 2; // ud2
        } else if (isTranslate && translateCallbackPtr) {
            auto* cb = reinterpret_cast<Func<Array<u8>(const Array<u8>&)>*>(translateCallbackPtr);
            Array<u8> srcBytes;
            srcBytes.set(code + pos, len);
            Array<u8> replacement = (*cb)(srcBytes);
            insnOutputSize = replacement.size();
        } else if (isHooked && hookBanned && !callbackPtr) {
            insnOutputSize = 2; // ud2
        } else if (isHooked && callbackPtr) {
            insnOutputSize = 64 + (hookBanned ? 0 : len);
        } else if (isIndirectJump) {
            insnOutputSize = kIndirectJumpCheckStubSize + len; // Dynamic JIT target check & translation stub
        } else if (isRet) {
            insnOutputSize = kRetCheckStubSize;
        } else if (isRelJmp) {
            bool inPage = isTargetInPage(pos, len, relOffset, relSize, code, codeSize);
            if (inPage) {
                if (relSize == 1) {
                    u8 op = code[pos + len - 2];
                    if (op == 0xEB) {
                        insnOutputSize = 5; // E9 disp32
                    } else if (op >= 0x70 && op <= 0x7F) {
                        insnOutputSize = 6; // 0F 8x disp32
                    } else {
                        insnOutputSize = len;
                    }
                } else {
                    insnOutputSize = len;
                }
            } else {
                u8 op = code[pos + relOffset - 1];
                bool isCond = false;
                if (relSize == 4) {
                    if (op >= 0x80 && op <= 0x8F && relOffset >= 2 && code[pos + relOffset - 2] == 0x0F) {
                        isCond = true;
                    }
                } else if (relSize == 1) {
                    if (op >= 0x70 && op <= 0x7F) {
                        isCond = true;
                    }
                }

                if (isCond) {
                    insnOutputSize = 2 + kDirectCrossPageStubSize; // opposite Jcc + stub
                } else {
                    insnOutputSize = kDirectCrossPageStubSize; // stub directly
                }
            }
        } else if (hasMem) {
            insnOutputSize = 60 + info.leaSize + len;
        } else {
            insnOutputSize = len;
        }

        if (isRspModifying && !isBannedPrivileged && !(isHooked && hookBanned) && !isIndirectJump && !isRet) {
            insnOutputSize += kStackCheckStubSize;
        }
        outputSize += insnOutputSize;

        ++insnCount;
        pos += len;
    }

    // 1. Identify branch targets to divide basic blocks.
    bool* isBranchTarget = static_cast<bool*>(std::calloc(codeSize, sizeof(bool)));
    if (isBranchTarget) {
        for (usz j = 0; j < insnCount; ++j) {
            const InsnInfo& info = insns[j];
            if (info.isRelJmp) {
                i64 origDisp = 0;
                if (info.relSize == 4) {
                    i32 disp32 = 0;
                    std::memcpy(&disp32, code + info.origOffset + info.relOffset, 4);
                    origDisp = disp32;
                } else if (info.relSize == 1) {
                    i8 disp8 = static_cast<i8>(code[info.origOffset + info.relOffset]);
                    origDisp = disp8;
                }
                usz origTarget = info.origOffset + info.instrLen + static_cast<usz>(origDisp);
                if (origTarget < codeSize) {
                    isBranchTarget[origTarget] = true;
                }
            }
        }
    }

    // 2. Perform Redundant Bounds Check Elimination (BCE)
    struct TrackedRange {
        i64 startOffset = 0;
        i64 endOffset = 0;
        bool valid = false;
    };
    TrackedRange rspChecked;
    TrackedRange rbpChecked;

    for (usz j = 0; j < insnCount; ++j) {
        InsnInfo& info = insns[j];
        
        if (isBranchTarget && isBranchTarget[info.origOffset]) {
            rspChecked.valid = false;
            rbpChecked.valid = false;
        }

        if (info.hasMem) {
            MemOperand op;
            parseMemOperand(code + info.origOffset, info.instrLen, info.immSize, op);
            if (op.valid) {
                if (op.baseReg == 4 && op.indexReg == -1) { // RSP
                    bool redundant = false;
                    if (rspChecked.valid && op.disp >= rspChecked.startOffset && 
                        op.disp + (i64)info.accessSize <= rspChecked.endOffset) {
                        redundant = true;
                    }
                    if (redundant) {
                        info.hasMem = false;
                    } else {
                        if (!rspChecked.valid) {
                            rspChecked.startOffset = op.disp;
                            rspChecked.endOffset = op.disp + info.accessSize;
                            rspChecked.valid = true;
                        } else {
                            rspChecked.startOffset = (rspChecked.startOffset < op.disp) ? rspChecked.startOffset : op.disp;
                            i64 accessEnd = op.disp + info.accessSize;
                            rspChecked.endOffset = (rspChecked.endOffset > accessEnd) ? rspChecked.endOffset : accessEnd;
                        }
                    }
                } else if (op.baseReg == 5 && op.indexReg == -1) { // RBP
                    bool redundant = false;
                    if (rbpChecked.valid && op.disp >= rbpChecked.startOffset && 
                        op.disp + (i64)info.accessSize <= rbpChecked.endOffset) {
                        redundant = true;
                    }
                    if (redundant) {
                        info.hasMem = false;
                    } else {
                        if (!rbpChecked.valid) {
                            rbpChecked.startOffset = op.disp;
                            rbpChecked.endOffset = op.disp + info.accessSize;
                            rbpChecked.valid = true;
                        } else {
                            rbpChecked.startOffset = (rbpChecked.startOffset < op.disp) ? rbpChecked.startOffset : op.disp;
                            i64 accessEnd = op.disp + info.accessSize;
                            rbpChecked.endOffset = (rbpChecked.endOffset > accessEnd) ? rbpChecked.endOffset : accessEnd;
                        }
                    }
                }
            }
        }

        if (info.isRspModifying) {
            rspChecked.valid = false;
        }
        
        usz prefixLen = 0, modrmPos = 0, opcodeLen = 0;
        bool hasRex = false;
        u8 rex = 0;
        parseLayout(code + info.origOffset, info.instrLen, prefixLen, hasRex, rex, opcodeLen, modrmPos);
        bool hasModRM = (modrmPos > 0 && modrmPos < info.instrLen);
        u8 modrm = hasModRM ? code[info.origOffset + modrmPos] : 0;
        bool twoByte = (opcodeLen == 2 && code[prefixLen + (hasRex ? 1 : 0)] == 0x0F);
        u8 opc = code[prefixLen + (hasRex ? 1 : 0)];
        if (modifiesRbp(code + info.origOffset, info.instrLen, hasModRM, modrm, opc, twoByte, hasRex, rex)) {
            rbpChecked.valid = false;
        }

        if (info.isRelJmp || info.isRet || info.isIndirectJump || info.isBannedPrivileged) {
            rspChecked.valid = false;
            rbpChecked.valid = false;
        }
    }

    if (isBranchTarget) {
        std::free(isBranchTarget);
    }

    // 3. Recalculate outputSize and outputOffset for each instruction
    usz currentOutputSize = 0;
    for (usz j = 0; j < insnCount; ++j) {
        InsnInfo& info = insns[j];
        info.outputOffset = currentOutputSize;
        
        usz insnOutputSize = 0;
        if (info.isBannedPrivileged) {
            insnOutputSize = 2; // ud2
        } else if (info.isTranslate && info.translateCallbackPtr) {
            auto* cb = reinterpret_cast<Func<Array<u8>(const Array<u8>&)>*>(info.translateCallbackPtr);
            Array<u8> srcBytes;
            srcBytes.set(code + info.origOffset, info.instrLen);
            Array<u8> replacement = (*cb)(srcBytes);
            insnOutputSize = replacement.size();
        } else if (info.isHooked && info.banned && !info.callbackPtr) {
            insnOutputSize = 2; // ud2
        } else if (info.isHooked && info.callbackPtr) {
            insnOutputSize = 64 + (info.banned ? 0 : info.instrLen);
        } else if (info.isIndirectJump) {
            insnOutputSize = kIndirectJumpCheckStubSize + info.instrLen;
        } else if (info.isRet) {
            insnOutputSize = kRetCheckStubSize;
        } else if (info.isRelJmp) {
            bool inPage = isTargetInPage(info.origOffset, info.instrLen, info.relOffset, info.relSize, code, codeSize);
            if (inPage) {
                if (info.relSize == 1) {
                    u8 op = code[info.origOffset + info.instrLen - 2];
                    if (op == 0xEB) {
                        insnOutputSize = 5; // E9 disp32
                    } else if (op >= 0x70 && op <= 0x7F) {
                        insnOutputSize = 6; // 0F 8x disp32
                    } else {
                        insnOutputSize = info.instrLen;
                    }
                } else {
                    insnOutputSize = info.instrLen;
                }
            } else {
                u8 op = code[info.origOffset + info.relOffset - 1];
                bool isCond = false;
                if (info.relSize == 4) {
                    if (op >= 0x80 && op <= 0x8F && info.relOffset >= 2 && code[info.origOffset + info.relOffset - 2] == 0x0F) {
                        isCond = true;
                    }
                } else if (info.relSize == 1) {
                    if (op >= 0x70 && op <= 0x7F) {
                        isCond = true;
                    }
                }

                if (isCond) {
                    insnOutputSize = 2 + kDirectCrossPageStubSize;
                } else {
                    insnOutputSize = kDirectCrossPageStubSize;
                }
            }
        } else if (info.hasMem) {
            insnOutputSize = 60 + info.leaSize + info.instrLen;
        } else {
            insnOutputSize = info.instrLen;
        }

        if (info.isRspModifying && !info.isBannedPrivileged && !(info.isHooked && info.banned) && !info.isIndirectJump && !info.isRet) {
            insnOutputSize += kStackCheckStubSize;
        }
        currentOutputSize += insnOutputSize;
    }
    outputSize = currentOutputSize;

    u8* output = nullptr;
    u32* offsetMap = nullptr;
#ifndef _WIN32
    usz mapSize = (outputSize + 4095) & ~4095;
    void* hint = nullptr;
    if (taskBase != 0) {
        hint = reinterpret_cast<void*>(taskBase + 0x10000000);
    }
    output = static_cast<u8*>(::mmap(hint, mapSize, PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_PRIVATE, -1, 0));
    if (output == MAP_FAILED) {
        std::free(insns);
        return result;
    }
    offsetMap = new u32[codeSize];
    for (usz i = 0; i < codeSize; ++i) offsetMap[i] = 0xFFFFFFFF;
#else
    output = static_cast<u8*>(std::malloc(outputSize));
    if (!output) {
        std::free(insns);
        return result;
    }
    offsetMap = new u32[codeSize];
    for (usz i = 0; i < codeSize; ++i) offsetMap[i] = 0xFFFFFFFF;
#endif

    u64 boundsCheckAddr = reinterpret_cast<u64>(&xi_sfi_bounds_check);
    u64 writeCheckAddr = reinterpret_cast<u64>(&xi_sfi_write_check);
    u64 jumpCheckAddr = reinterpret_cast<u64>(&xi_sfi_jump_check);
    u64 stackCheckAddr = reinterpret_cast<u64>(&xi_sfi_stack_check);
    u64 indirectResolverAddr = reinterpret_cast<u64>(&xi_sfi_indirect_jump_resolver);
    u8* outPtr = output;

    for (usz i = 0; i < insnCount; ++i) {
        const InsnInfo& info = insns[i];
        u8* beforePtr = outPtr;
        const u8* insnSrc = code + info.origOffset;

        if (info.origOffset < codeSize) {
            offsetMap[info.origOffset] = static_cast<u32>(outPtr - output);
        }



        if (info.isBannedPrivileged) {
            emitUd2(outPtr);
            continue;
        }

        if (info.isTranslate && info.translateCallbackPtr) {
            auto* cb = reinterpret_cast<Func<Array<u8>(const Array<u8>&)>*>(info.translateCallbackPtr);
            Array<u8> srcBytes;
            srcBytes.set(insnSrc, info.instrLen);
            Array<u8> replacement = (*cb)(srcBytes);
            std::memcpy(outPtr, replacement.data(), replacement.size());
            outPtr += replacement.size();
            continue;
        }

        if (info.isHooked && info.banned && !info.callbackPtr) {
            emitUd2(outPtr);
            continue;
        }

        if (info.isIndirectJump) {
            emitIndirectJumpStub(outPtr, insnSrc, info.instrLen, indirectResolverAddr);
            continue;
        }

        if (info.isRet) {
            emitRetCheckStub(outPtr, indirectResolverAddr);
            continue;
        }

        if (info.isHooked && info.callbackPtr) {
            u64 callbackAddr = reinterpret_cast<u64>(info.callbackPtr);
            u64 helperAddr = reinterpret_cast<u64>(&xi_run_instruction_callback);
            if (info.callbackPtr == reinterpret_cast<void*>(1)) {
                TaskState* curTask = xi_get_current_task();
                callbackAddr = reinterpret_cast<u64>(curTask);
                helperAddr = reinterpret_cast<u64>(&xi_emulate_syscall_helper);
            }
            emitCallbackCall(outPtr, callbackAddr, helperAddr);
            if (!info.banned) {
                std::memcpy(outPtr, insnSrc, info.instrLen);
                outPtr += info.instrLen;
            }
            if (info.isRspModifying) {
                emitStackCheckStub(outPtr, stackCheckAddr);
            }

            continue;
        }

        if (info.hasMem) {
            if (isStoreInstruction(insnSrc, info.instrLen)) {
                emitBoundsCheckStub(outPtr, writeCheckAddr, insnSrc, info.instrLen, info.immSize, info.accessSize);
            } else {
                emitBoundsCheckStub(outPtr, boundsCheckAddr, insnSrc, info.instrLen, info.immSize, info.accessSize);
            }
        }

        if (info.isRelJmp) {
            bool inPage = isTargetInPage(info.origOffset, info.instrLen, info.relOffset, info.relSize, code, codeSize);
            i64 origDisp = 0;
            if (info.relSize == 4) {
                i32 disp32 = 0;
                std::memcpy(&disp32, insnSrc + info.relOffset, 4);
                origDisp = disp32;
            } else if (info.relSize == 1) {
                origDisp = static_cast<i8>(insnSrc[info.relOffset]);
            }
            ::printf("[AOT Rewrite] Relative branch at offset 0x%lx: instrLen=%lu, relSize=%lu, inPage=%d, target=0x%lx\n",
                     (unsigned long)info.origOffset, (unsigned long)info.instrLen, (unsigned long)info.relSize, (int)inPage,
                     (unsigned long)(taskBase + info.origOffset + info.instrLen + origDisp));
            ::fflush(stdout);

            if (inPage) {
                if (info.relSize == 4) {
                    usz insnRelOffset = info.relOffset;
                    std::memcpy(outPtr, insnSrc, insnRelOffset);

                    i32 origDisp = 0;
                    std::memcpy(&origDisp, insnSrc + insnRelOffset, 4);

                    usz origTarget = info.origOffset + info.instrLen + static_cast<usz>(
                        static_cast<i64>(origDisp));

                    usz newTargetOffset = 0;
                    bool targetResolved = false;
                    for (usz j = 0; j < insnCount; ++j) {
                        if (insns[j].origOffset == origTarget) {
                            newTargetOffset = insns[j].outputOffset;
                            targetResolved = true;
                            break;
                        }
                    }
                    if (!targetResolved) {
                        *outPtr++ = 0x0F;
                        *outPtr++ = 0x0B;
                        for (usz k = 2; k < info.instrLen; ++k) {
                            *outPtr++ = 0x90;
                        }
                        continue;
                    }

                    usz curInsnOutputEnd = info.outputOffset + info.instrLen;
                    if (info.hasMem) {
                        curInsnOutputEnd += 60 + info.leaSize;
                    }

                    i64 newDisp64 = static_cast<i64>(newTargetOffset) -
                                    static_cast<i64>(curInsnOutputEnd);
                    i32 newDisp = static_cast<i32>(newDisp64);
                    std::memcpy(outPtr + insnRelOffset, &newDisp, 4);

                    usz afterDisp = insnRelOffset + 4;
                    if (afterDisp < info.instrLen) {
                        std::memcpy(outPtr + afterDisp, insnSrc + afterDisp,
                                    info.instrLen - afterDisp);
                    }
                    outPtr += info.instrLen;

                } else if (info.relSize == 1 &&
                           (insnSrc[info.instrLen - 2] == 0xEB || (insnSrc[info.instrLen - 2] >= 0x70 && insnSrc[info.instrLen - 2] <= 0x7F))) {
                    u8 op = insnSrc[info.instrLen - 2];
                    i8 origDisp = static_cast<i8>(insnSrc[info.instrLen - 1]);
                    usz origTarget = info.origOffset + info.instrLen + static_cast<usz>(
                        static_cast<i64>(origDisp));

                    usz newTargetOffset = 0;
                    bool targetResolved = false;
                    for (usz j = 0; j < insnCount; ++j) {
                        if (insns[j].origOffset == origTarget) {
                            newTargetOffset = insns[j].outputOffset;
                            targetResolved = true;
                            break;
                        }
                    }
                    if (!targetResolved) {
                        usz expectedSize = (op == 0xEB ? 5 : 6);
                        *outPtr++ = 0x0F;
                        *outPtr++ = 0x0B;
                        for (usz k = 2; k < expectedSize; ++k) {
                            *outPtr++ = 0x90;
                        }
                        continue;
                    }

                    usz curInsnOutputEnd = info.outputOffset + (op == 0xEB ? 5 : 6);
                    i64 newDisp64 = static_cast<i64>(newTargetOffset) -
                                    static_cast<i64>(curInsnOutputEnd);
                    i32 newDisp = static_cast<i32>(newDisp64);

                    if (op == 0xEB) {
                        *outPtr++ = 0xE9;
                        std::memcpy(outPtr, &newDisp, 4);
                        outPtr += 4;
                    } else {
                        *outPtr++ = 0x0F;
                        *outPtr++ = 0x80 + (op & 0x0F);
                        std::memcpy(outPtr, &newDisp, 4);
                        outPtr += 4;
                    }
                }
            } else {
                // Cross-page branch!
                i64 origDisp = 0;
                if (info.relSize == 4) {
                    i32 disp32 = 0;
                    std::memcpy(&disp32, insnSrc + info.relOffset, 4);
                    origDisp = disp32;
                } else if (info.relSize == 1) {
                    i8 disp8 = static_cast<i8>(insnSrc[info.relOffset]);
                    origDisp = disp8;
                }
                u64 targetGuestVirtualAddr = taskBase + info.origOffset + info.instrLen + origDisp;

                u8 op = insnSrc[info.relOffset - 1];
                bool isCond = false;
                bool isCall = false;
                if (info.relSize == 4) {
                    if (op >= 0x80 && op <= 0x8F && info.relOffset >= 2 && insnSrc[info.relOffset - 2] == 0x0F) {
                        isCond = true;
                    } else if (op == 0xE8) {
                        isCall = true;
                    }
                } else if (info.relSize == 1) {
                    if (op >= 0x70 && op <= 0x7F) {
                        isCond = true;
                    }
                }

                if (isCond) {
                    u8 cond = op & 0x0F;
                    *outPtr++ = 0x70 + (cond ^ 1);
                    *outPtr++ = kDirectCrossPageStubSize;
                    emitDirectCrossPageStub(outPtr, targetGuestVirtualAddr, indirectResolverAddr, false);
                } else {
                    emitDirectCrossPageStub(outPtr, targetGuestVirtualAddr, indirectResolverAddr, isCall);
                }
            }
        } else {
            usz prefixLen = 0, modrmPos = 0, opcodeLen = 0;
            bool hasRex = false;
            u8 rex = 0;
            parseLayout(insnSrc, info.instrLen, prefixLen, hasRex, rex, opcodeLen, modrmPos);
            bool isRipRel = false;
            if (modrmPos > 0 && modrmPos < info.instrLen) {
                u8 modrm = insnSrc[modrmPos];
                u8 mod = (modrm >> 6) & 3;
                u8 rm = modrm & 7;
                if (mod == 0 && rm == 5) {
                    usz dispPos = modrmPos + 1;
                    if (dispPos + 4 <= info.instrLen) {
                        i32 origDisp = 0;
                        std::memcpy(&origDisp, insnSrc + dispPos, 4);
                        
                        usz origRip = taskBase + info.origOffset + info.instrLen;
                        usz targetAddr = origRip + static_cast<i64>(origDisp);
                        
                        usz newRip = reinterpret_cast<usz>(outPtr) + info.instrLen;
                        i64 newDisp64 = static_cast<i64>(targetAddr) - static_cast<i64>(newRip);
                        i32 newDisp = static_cast<i32>(newDisp64);
                        
                        std::memcpy(outPtr, insnSrc, info.instrLen);
                        std::memcpy(outPtr + dispPos, &newDisp, 4);
                        outPtr += info.instrLen;
                        isRipRel = true;
                    }
                }
            }
            if (!isRipRel) {
                std::memcpy(outPtr, insnSrc, info.instrLen);
                outPtr += info.instrLen;
            }
        }

        if (info.isRspModifying) {
            emitStackCheckStub(outPtr, stackCheckAddr);
        }
    }

    if (taskBase == 0x7006e000) {
        ::printf("[AOT Debug] Page 0x7006e000 JIT disassembly/info:\n");
        for (usz i = 0; i < insnCount; ++i) {
            const InsnInfo& info = insns[i];
            if (info.origOffset >= 0x930) {
                ::printf("  Offset 0x%lx (Output 0x%lx, len %lu, relJmp=%d, ret=%d, mem=%d, rspMod=%d): ",
                         (unsigned long)info.origOffset, (unsigned long)info.outputOffset, (unsigned long)info.instrLen,
                         (int)info.isRelJmp, (int)info.isRet, (int)info.hasMem, (int)info.isRspModifying);
                for (usz j = 0; j < info.instrLen; ++j) {
                    ::printf("%02x ", code[info.origOffset + j]);
                }
                ::printf("\n");
            }
        }
        ::fflush(stdout);
    }

    std::free(insns);

#ifndef _WIN32
    int err = mprotect(output, (outputSize + 4095) & ~4095, PROT_READ | PROT_EXEC);
    if (err != 0) {
        ::printf("mprotect failed! err: %d, errno: %d\n", err, errno);
    }
#endif

    result.patchedCode = output;
    result.patchedSize = static_cast<usz>(outPtr - output);
    result.originalSize = codeSize;
    result.offsetMap = offsetMap;
    result.success = true;
    return result;
}

// -------------------------------------------------------------------------
// AOT Cache Operations
// -------------------------------------------------------------------------

AOTRegion* AOT::findCached(Array<AOTRegion>& cache, usz addr, usz size) {
    for (usz i = 0; i < cache.size(); ++i) {
        AOTRegion& region = cache[i];
        if (region.originalAddr == addr && region.originalSize == size) {
            return &region;
        }
    }
    return nullptr;
}

void AOT::freePatchedCode(u8* patchedCode, usz patchedSize) {
    if (patchedCode) {
#ifndef _WIN32
        ::munmap(patchedCode, (patchedSize + 4095) & ~4095);
#else
        std::free(patchedCode);
#endif
    }
}

void AOT::invalidate(Array<AOTRegion>& cache, usz addr, usz size) {
    usz end = addr + size;

    // Walk backwards so removal doesn't invalidate indices.
    for (usz i = cache.size(); i > 0; --i) {
        usz idx = i - 1;
        AOTRegion& region = cache[idx];
        usz regionEnd = region.originalAddr + region.originalSize;

        // Check for overlap: [addr, end) ∩ [originalAddr, regionEnd).
        if (region.originalAddr < end && regionEnd > addr) {
            // Free the patched code buffer.
            if (region.patchedCode) {
                freePatchedCode(region.patchedCode, region.patchedSize);
            }
            if (region.offsetMap) {
                delete[] region.offsetMap;
            }
            // Remove from cache by shifting elements.
            for (usz j = idx; j + 1 < cache.size(); ++j) {
                cache[j] = cache[j + 1];
            }
            cache.pop();
        }
    }
}

void AOT::destroyCache(Array<AOTRegion>& cache) {
    for (usz i = 0; i < cache.size(); ++i) {
        if (cache[i].patchedCode) {
            freePatchedCode(cache[i].patchedCode, cache[i].patchedSize);
            cache[i].patchedCode = nullptr;
        }
        if (cache[i].offsetMap) {
            delete[] cache[i].offsetMap;
            cache[i].offsetMap = nullptr;
        }
    }
    cache.clear();
}

bool AOT::verifyStubSizes() {
    u8 buffer[256];
    
    // 1. Verify kRetCheckStubSize
    u8* ptr = buffer;
    emitRetCheckStub(ptr, 0x123456789ABCDEF0ULL);
    usz retSize = ptr - buffer;
    if (retSize != kRetCheckStubSize) {
        ::printf("[AOT Verification] kRetCheckStubSize mismatch: expected %lu, got %lu\n",
                 (unsigned long)kRetCheckStubSize, (unsigned long)retSize);
        return false;
    }

    // 2. Verify kIndirectJumpCheckStubSize
    u8 mockJump[] = { 0xFF, 0x20 };
    ptr = buffer;
    emitIndirectJumpStub(ptr, mockJump, 2, 0x123456789ABCDEF0ULL);
    usz indJumpSize = ptr - buffer;
    if (indJumpSize != kIndirectJumpCheckStubSize + 2) {
        ::printf("[AOT Verification] kIndirectJumpCheckStubSize mismatch: expected %lu, got %lu\n",
                 (unsigned long)kIndirectJumpCheckStubSize, (unsigned long)(indJumpSize - 2));
        return false;
    }

    // 3. Verify kDirectCrossPageStubSize
    ptr = buffer;
    emitDirectCrossPageStub(ptr, 0x1122334455667788ULL, 0x123456789ABCDEF0ULL, false);
    usz crossPageSize = ptr - buffer;
    if (crossPageSize != kDirectCrossPageStubSize) {
        ::printf("[AOT Verification] kDirectCrossPageStubSize mismatch: expected %lu, got %lu\n",
                 (unsigned long)kDirectCrossPageStubSize, (unsigned long)crossPageSize);
        return false;
    }

    // 4. Verify kStackCheckStubSize
    ptr = buffer;
    emitStackCheckStub(ptr, 0x123456789ABCDEF0ULL);
    usz stackCheckSize = ptr - buffer;
    if (stackCheckSize != kStackCheckStubSize) {
        ::printf("[AOT Verification] kStackCheckStubSize mismatch: expected %lu, got %lu\n",
                 (unsigned long)kStackCheckStubSize, (unsigned long)stackCheckSize);
        return false;
    }

    return true;
}

} // namespace Task

#endif // defined(__x86_64__) || defined(_M_X64)
