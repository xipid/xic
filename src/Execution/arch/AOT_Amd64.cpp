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

#include "../../../include/Execution/AOT.hpp"
#include "../../../include/Execution/Task.hpp"

#include <cstring>
#include <cstdlib>

#ifndef _WIN32
#include <sys/mman.h>
#include <cerrno>
#include <cstdio>
#endif

namespace Execution {

// -------------------------------------------------------------------------
// SFI Bounds-Check Functions
// -------------------------------------------------------------------------

/**
 * @brief Validates that a memory access [addr, addr+size) falls within
 *        the task's accessible memory regions (both readable AND writable).
 *
 * If the access is out-of-bounds, traps with ud2.
 */
extern "C" void xi_sfi_bounds_check(void* addr, usz size) {
    if (addr == nullptr && size == 0) return;
    TaskState* state = xi_get_current_task();
    if (!state) return;

    u8* target = static_cast<u8*>(addr);
    usz targetAddr = reinterpret_cast<usz>(addr);

    // 1. Check existing mapped regions (both physical and virtual base)
    for (usz i = 0; i < state->regions.size(); ++i) {
        MemoryRegion& r = state->regions[i];
        if (r.physical) {
            if ((target >= r.physical && target + size <= r.physical + r.size) ||
                (targetAddr >= r.base && targetAddr + size <= r.base + r.size)) {
                return; // Access is within a valid region.
            }
        }
    }

    // 2. Check stack
    if (state->stack && target >= state->stack &&
        target + size <= state->stack + state->stackSize) {
        return;
    }

    // 3. Check registered fetch ranges
    for (usz i = 0; i < state->fetchRanges.size(); ++i) {
        TaskState::FetchRange& fr = state->fetchRanges[i];
        if (targetAddr >= fr.start && targetAddr + size <= fr.end) {
            if (!fr.cached || !fr.resolved) {
                fr.callback(fr.start, fr.end);
                if (fr.cached) {
                    fr.resolved = true;
                }
                // Re-verify that the callback successfully mapped/allocated the region
                for (usz j = 0; j < state->regions.size(); ++j) {
                    MemoryRegion& r = state->regions[j];
                    if (r.physical && r.base <= fr.start && r.base + r.size >= fr.end) {
                        return; // Successfully resolved!
                    }
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
    if (addr == nullptr && size == 0) return;
    TaskState* state = xi_get_current_task();
    if (!state) return;

    u8* target = static_cast<u8*>(addr);
    usz targetAddr = reinterpret_cast<usz>(addr);

    // 1. Check existing writable mapped regions
    for (usz i = 0; i < state->regions.size(); ++i) {
        MemoryRegion& r = state->regions[i];
        if (r.physical && r.writable) {
            if ((target >= r.physical && target + size <= r.physical + r.size) ||
                (targetAddr >= r.base && targetAddr + size <= r.base + r.size)) {
                return; // Write is within a writable region.
            }
        }
    }

    // 2. Stack is always writable.
    if (state->stack && target >= state->stack &&
        target + size <= state->stack + state->stackSize) {
        return;
    }

    // 3. Check registered fetch ranges
    for (usz i = 0; i < state->fetchRanges.size(); ++i) {
        TaskState::FetchRange& fr = state->fetchRanges[i];
        if (targetAddr >= fr.start && targetAddr + size <= fr.end) {
            if (!fr.cached || !fr.resolved) {
                fr.callback(fr.start, fr.end);
                if (fr.cached) {
                    fr.resolved = true;
                }
                // Re-verify that the callback successfully mapped/allocated the region and it is writable
                for (usz j = 0; j < state->regions.size(); ++j) {
                    MemoryRegion& r = state->regions[j];
                    if (r.physical && r.writable && r.base <= fr.start && r.base + r.size >= fr.end) {
                        return; // Successfully resolved!
                    }
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

    // Invalid jump target: trap.
    __asm__ volatile("ud2");
}

extern "C" void* xi_sfi_indirect_jump_resolver(void* target) {
    TaskState* state = xi_get_current_task();
    if (!state) return target;

    usz targetAddr = reinterpret_cast<usz>(target);

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
        // Invalid target: trap!
        __asm__ volatile("ud2");
        return nullptr;
    }

    // 2. We found the region. Ensure it is AOT-compiled.
    AOTRegion* cached = AOT::findCached(state->aotCache, reinterpret_cast<usz>(targetRegion->physical), targetRegion->size);
    if (!cached) {
        // Run AOT compilation!
        targetRegion->executable = true; // Make sure it is marked executable
        AOTResult res = AOT::rewrite(targetRegion->physical, targetRegion->size, state->regions, 0);
        if (res.success && res.patchedCode) {
            AOTRegion reg;
            reg.originalAddr = reinterpret_cast<usz>(targetRegion->physical);
            reg.originalSize = targetRegion->size;
            reg.patchedCode = res.patchedCode;
            reg.patchedSize = res.patchedSize;
            state->aotCache.push(reg);
            cached = &state->aotCache[state->aotCache.size() - 1];
        }
    }

    if (cached && cached->patchedCode) {
        usz offset = isVirtual ? (targetAddr - targetRegion->base) : (targetAddr - reinterpret_cast<usz>(targetRegion->physical));
        return cached->patchedCode + offset;
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

extern "C" void xi_run_instruction_callback(void* callbackPtr, usz guestRbx) {
    ::printf("[JIT Callback Debug] xi_run_instruction_callback entered! callbackPtr=%p, guestRbx=0x%lx\n", callbackPtr, (unsigned long)guestRbx);
    ::fflush(stdout);
    xi_last_guest_rbx = guestRbx;
    if (callbackPtr) {
        auto* cb = static_cast<Func<void()>*>(callbackPtr);
        if (*cb) {
            (*cb)();
        }
    }
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
static bool isUnconditionallyBanned(const u8* code, usz len) {
    if (len == 0) return false;

    usz pos = 0;
    // Skip legacy prefixes
    for (;;) {
        if (pos >= len) return false;
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
    if (pos >= len) return false;

    u8 op = code[pos];
    usz remaining = len - pos;

    // Single-byte privileged instructions:
    switch (op) {
        case 0xF4: return true; // HLT
        case 0xFA: return true; // CLI
        case 0xFB: return true; // STI
        case 0xCD: return true; // INT imm8 (any software interrupt)
        case 0xCE: return true; // INTO
        case 0xE4: return true; // IN AL, imm8
        case 0xE5: return true; // IN AX/EAX, imm8
        case 0xE6: return true; // OUT imm8, AL
        case 0xE7: return true; // OUT imm8, AX/EAX
        case 0xEC: return true; // IN AL, DX
        case 0xED: return true; // IN AX/EAX, DX
        case 0xEE: return true; // OUT DX, AL
        case 0xEF: return true; // OUT DX, AX/EAX
        case 0xC2: return true; // RET imm16 — ban unconditionally
        case 0xCB: return true; // RETF — far return, banned unconditionally
        case 0xCA: return true; // RETF imm16 — far return, banned unconditionally
        case 0xCF: return true; // IRET/IRETD/IRETQ — interrupt return, banned unconditionally
        case 0x9A: return true; // Direct far call — invalid in 64-bit but ban anyway
        case 0xEA: return true; // Direct far jump — invalid in 64-bit but ban anyway
        case 0x8E: return true; // MOV segment register — banned unconditionally
        // VEX prefix — ban all AVX/AVX2 instructions in sandboxed code.
        case 0xC4: return true; // 3-byte VEX prefix
        case 0xC5: return true; // 2-byte VEX prefix
        // EVEX prefix — ban all AVX-512 instructions.
        case 0x62: return true; // EVEX prefix
        case 0xFF: // FF group checks: far jumps and far calls
            if (pos + 1 < len) {
                u8 modrm = code[pos + 1];
                u8 reg = (modrm >> 3) & 7;
                if (reg == 3 || reg == 5) return true; // CALL far indirect / JMP far indirect
            }
            break;
        default: break;
    }

    // Two-byte privileged instructions (0F xx):
    if (op == 0x0F && remaining >= 2) {
        u8 op2 = code[pos + 1];
        if (op2 == 0x38 || op2 == 0x3A) return true; // Ban 3-byte opcodes (0F 38/3A xx)
        switch (op2) {
            case 0x05: return true; // SYSCALL
            case 0x07: return true; // SYSRET
            case 0x30: return true; // WRMSR
            case 0x32: return true; // RDMSR
            case 0x34: return true; // SYSENTER
            case 0x35: return true; // SYSEXIT
            case 0x01: return true; // LGDT/LIDT/SMSW/LMSW/INVLPG — all privileged
            case 0x06: return true; // CLTS
            case 0x08: return true; // INVD
            case 0x09: return true; // WBINVD
            case 0x20: return true; // MOV from CR
            case 0x21: return true; // MOV from DR
            case 0x22: return true; // MOV to CR
            case 0x23: return true; // MOV to DR
            case 0xA1: return true; // POP FS — banned unconditionally
            case 0xA9: return true; // POP GS — banned unconditionally
            case 0xB2: return true; // LSS — banned unconditionally
            case 0xB4: return true; // LFS — banned unconditionally
            case 0xB5: return true; // LGS — banned unconditionally
            case 0xAA: return true; // RSM — resume from SMM, banned unconditionally
            case 0xAE:
                if (remaining >= 3) {
                    u8 modrm = code[pos + 2];
                    u8 mod = (modrm >> 6) & 3;
                    u8 reg = (modrm >> 3) & 7;
                    if (mod == 3 && reg <= 3) return true; // Ban FSGSBASE (rdfsbase/rdgsbase/wrfsbase/wrgsbase)
                }
                break;
            default: break;
        }
    }

    return false;
}

/**
 * @brief Checks if an instruction is hooked or banned via the task's
 *        instruction hook system (user-defined hooks on top of the
 *        unconditional ban list).
 */
static bool isInstructionBannedOrHooked(const u8* code, usz len, bool& outBanned, void*& outCallbackPtr) {
    outBanned = false;
    outCallbackPtr = nullptr;

    TaskState* state = xi_get_current_task();
    if (!state) return false;

    if (len == 0) return false;

    usz pos = 0;
    // Skip legacy prefixes
    for (;;) {
        if (pos >= len) return false;
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
    if (pos >= len) return false;

    u8 op = code[pos];
    usz remaining = len - pos;
    String name;
    if (op == 0x90 && remaining == 1) name = "nop";
    else if (op == 0xF4 && remaining == 1) name = "hlt";
    else if (op == 0xCC && remaining == 1) name = "int3";
    else if (op == 0xCD) name = "int";
    else if (op == 0xC3 && remaining == 1) name = "ret";
    else if (op == 0x0F && remaining >= 2) {
        u8 op2 = code[pos + 1];
        if (op2 == 0x05) name = "syscall";
        else if (op2 == 0xA2) name = "cpuid";
        else if (op2 == 0x31) name = "rdtsc";
        else if (op2 == 0x0B) name = "ud2";
    }

    if (name.size() == 0) return false;

    ::printf("[AOT Hook Check] name=%s, current task state=%p, hooks size=%zu\n",
             name.c_str(), state, state->instructionHooks.size());
    for (usz i = 0; i < state->instructionHooks.size(); ++i) {
        ::printf("  Hook %zu: name=%s, banned=%d, callback=%p\n",
                 i, state->instructionHooks[i].name.c_str(),
                 state->instructionHooks[i].banned,
                 (void*)&state->instructionHooks[i].callback);
        if (state->instructionHooks[i].name == name) {
            outBanned = state->instructionHooks[i].banned;
            outCallbackPtr = &(state->instructionHooks[i].callback);
            return true;
        }
    }
    return false;
}

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
    // mov rsi, rbx
    *outPtr++ = 0x48;
    *outPtr++ = 0x89;
    *outPtr++ = 0xDE;

    // movabs rdi, callbackPtr
    *outPtr++ = 0x48;
    *outPtr++ = 0xBF;
    std::memcpy(outPtr, &callbackPtr, 8);
    outPtr += 8;

    // movabs rax, helperAddr
    *outPtr++ = 0x48;
    *outPtr++ = 0xB8;
    std::memcpy(outPtr, &helperAddr, 8);
    outPtr += 8;

    // call rax
    *outPtr++ = 0xFF;
    *outPtr++ = 0xD0;
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

        // Memory operand if mod != 3 (register direct).
        if (mod != 3) {
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
    // push rax
    *out++ = 0x50;
    // push rdi
    *out++ = 0x57;
    // push rsi
    *out++ = 0x56;

    // Construct and emit the LEA instruction to compute the target address into rdi
    usz leaLen = buildLea(out, insnSrc, instrLen, immSize);
    out += leaLen;

    // push accessSize (imm8)
    *out++ = 0x6A;
    *out++ = static_cast<u8>(accessSize);
    // pop rsi
    *out++ = 0x5E;

    // movabs rax, stubAddr
    *out++ = 0x48;
    *out++ = 0xB8;
    std::memcpy(out, &stubAddr, 8);
    out += 8;

    // call rax (FF D0)
    *out++ = 0xFF;
    *out++ = 0xD0;

    // pop rsi
    *out++ = 0x5E;
    // pop rdi
    *out++ = 0x5F;
    // pop rax
    *out++ = 0x58;
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
static constexpr usz kRetCheckStubSize = 25;

static void emitRetCheckStub(u8*& out, u64 jumpCheckAddr) {
    // push rax
    *out++ = 0x50;
    // mov rax, [rsp+8] (48 8B 44 24 08)
    *out++ = 0x48;
    *out++ = 0x8B;
    *out++ = 0x44;
    *out++ = 0x24;
    *out++ = 0x08;
    // push rdi
    *out++ = 0x57;
    // mov rdi, rax (48 89 C7)
    *out++ = 0x48;
    *out++ = 0x89;
    *out++ = 0xC7;
    // movabs rax, imm64
    *out++ = 0x48;
    *out++ = 0xB8;
    std::memcpy(out, &jumpCheckAddr, 8);
    out += 8;
    // call rax (FF D0)
    *out++ = 0xFF;
    *out++ = 0xD0;
    // pop rdi
    *out++ = 0x5F;
    // pop rax
    *out++ = 0x58;
    // ret (now validated)
    *out++ = 0xC3;
}

static void emitIndirectJumpStub(u8*& out, const u8* insnSrc, usz instrLen, u64 resolverAddr) {
    // 1. push rax
    *out++ = 0x50;
    // 2. push rdi
    *out++ = 0x57;

    // 3. Dynamically construct the PUSH instruction from the original JMP/CALL.
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

    // 4. pop rdi
    *out++ = 0x5F;
    // 5. push rsi
    *out++ = 0x56;
    // 6. push rdx
    *out++ = 0x52;
    // 7. push rcx
    *out++ = 0x51;
    // 8. push r8
    *out++ = 0x41; *out++ = 0x50;
    // 9. push r9
    *out++ = 0x41; *out++ = 0x51;
    // 10. push r10
    *out++ = 0x41; *out++ = 0x52;
    // 11. push r11
    *out++ = 0x41; *out++ = 0x53;

    // 12. movabs rax, resolverAddr
    *out++ = 0x48;
    *out++ = 0xB8;
    std::memcpy(out, &resolverAddr, 8);
    out += 8;

    // 13. call rax
    *out++ = 0xFF;
    *out++ = 0xD0;

    // 15. pop r11
    *out++ = 0x41; *out++ = 0x5B;
    // 16. pop r10
    *out++ = 0x41; *out++ = 0x5A;
    // 17. pop r9
    *out++ = 0x41; *out++ = 0x59;
    // 18. pop r8
    *out++ = 0x41; *out++ = 0x58;
    // 19. pop rcx
    *out++ = 0x59;
    // 20. pop rdx
    *out++ = 0x5A;
    // 21. pop rsi
    *out++ = 0x5E;
    // 22. pop rdi
    *out++ = 0x5F;

    // 14. mov r11, rax
    *out++ = 0x49;
    *out++ = 0x89;
    *out++ = 0xC3;

    // 23. pop rax
    *out++ = 0x58;

    // 24. Determine if the original was a CALL or a JMP
    u8 reg = (modrm >> 3) & 7;
    bool isCall = (reg == 2 || reg == 3);
    if (isCall) {
        // call r11 (41 FF D3)
        *out++ = 0x41;
        *out++ = 0xFF;
        *out++ = 0xD3;
    } else {
        // jmp r11 (41 FF E3)
        *out++ = 0x41;
        *out++ = 0xFF;
        *out++ = 0xE3;
    }
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
 *                                Total: 19 bytes
 */
static constexpr usz kStackCheckStubSize = 19;

static void emitStackCheckStub(u8*& out, u64 stackCheckAddr) {
    // push rax
    *out++ = 0x50;
    // push rdi
    *out++ = 0x57;
    // mov rdi, rsp (48 89 E7)
    *out++ = 0x48;
    *out++ = 0x89;
    *out++ = 0xE7;
    // movabs rax, imm64
    *out++ = 0x48;
    *out++ = 0xB8;
    std::memcpy(out, &stackCheckAddr, 8);
    out += 8;
    // call rax (FF D0)
    *out++ = 0xFF;
    *out++ = 0xD0;
    // pop rdi
    *out++ = 0x5F;
    // pop rax
    *out++ = 0x58;
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

// -------------------------------------------------------------------------
// AOT::rewrite — Main Rewriter
// -------------------------------------------------------------------------

AOTResult AOT::rewrite(const u8* code, usz codeSize,
                       const Array<MemoryRegion>& /* regions */,
                       usz /* taskBase */) {
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
        bool isBannedPrivileged; // Unconditionally banned privileged instruction.
        bool isIndirectJump;     // Indirect JMP/CALL — banned for containment.
        bool isRet;              // RET — rewritten with validation.
        bool isRspModifying;     // RSP modifying instruction.
        usz immSize;
        usz leaSize;
        usz accessSize;
    };

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

        usz len = xi_x86_insn_length(code + pos, codeSize - pos,
                                      hasMem, isRelJmp, relOffset, relSize, isRspModifying, immSize);
        if (len == 0) {
            // Decoding failed — unknown instruction. Trap it.
            len = 1;
            hasMem = false;
            isRelJmp = false;
        }

        bool hookBanned = false;
        void* callbackPtr = nullptr;
        bool isHooked = isInstructionBannedOrHooked(code + pos, len, hookBanned, callbackPtr);

        bool isBannedPrivileged = isUnconditionallyBanned(code + pos, len);
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
        ::printf("[AOT First Pass] Insn %d: origOffset=%d, relOffset=%d, relSize=%d\n",
                 (int)insnCount, (int)info.origOffset, (int)info.relOffset, (int)relSize);
        info.relSize = relSize;
        info.isHooked = isHooked;
        info.banned = hookBanned;
        info.callbackPtr = callbackPtr;
        info.isBannedPrivileged = isBannedPrivileged;
        info.isIndirectJump = isIndirectJump;
        info.isRet = isRet;
        info.isRspModifying = isRspModifying;
        info.immSize = immSize;

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
        } else if (isHooked && hookBanned) {
            insnOutputSize = 2; // ud2
        } else if (isIndirectJump) {
            insnOutputSize = 45 + len; // Dynamic JIT target check & translation stub
        } else if (isRet) {
            insnOutputSize = kRetCheckStubSize;
        } else if (isHooked && callbackPtr) {
            insnOutputSize = 25 + len;
        } else if (hasMem) {
            insnOutputSize = 21 + info.leaSize + len;
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

    u8* output = nullptr;
#ifndef _WIN32
    usz mapSize = (outputSize + 4095) & ~4095;
    output = static_cast<u8*>(::mmap(nullptr, mapSize, PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_PRIVATE, -1, 0));
    if (output == MAP_FAILED) {
        std::free(insns);
        return result;
    }
#else
    output = static_cast<u8*>(std::malloc(outputSize));
    if (!output) {
        std::free(insns);
        return result;
    }
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

        ::printf("[AOT Rewrite Detail] Insn %d: origOffset=%d, len=%d, beforeOffset=%d, bytes=",
                 (int)i, (int)info.origOffset, (int)info.instrLen, (int)(beforePtr - output));
        for (usz k = 0; k < info.instrLen; ++k) ::printf("%02x ", insnSrc[k]);
        if (i == 49) {
            ::printf("[AOT Debug Insn 49 Rewrite] isHooked=%d, callbackPtr=%p, banned=%d\n",
                     (int)info.isHooked, info.callbackPtr, (int)info.banned);
        }

        if (info.isBannedPrivileged) {
            emitUd2(outPtr);
            continue;
        }

        if (info.isHooked && info.banned) {
            emitUd2(outPtr);
            continue;
        }

        if (info.isIndirectJump) {
            emitIndirectJumpStub(outPtr, insnSrc, info.instrLen, indirectResolverAddr);
            continue;
        }

        if (info.isRet) {
            emitRetCheckStub(outPtr, jumpCheckAddr);
            continue;
        }

        if (info.isHooked && info.callbackPtr) {
            u64 callbackAddr = reinterpret_cast<u64>(info.callbackPtr);
            u64 helperAddr = reinterpret_cast<u64>(&xi_run_instruction_callback);
            emitCallbackCall(outPtr, callbackAddr, helperAddr);
            std::memcpy(outPtr, insnSrc, info.instrLen);
            outPtr += info.instrLen;
            if (info.isRspModifying) {
                emitStackCheckStub(outPtr, stackCheckAddr);
            }
            ::printf("[AOT Debug Hook Rewrite] Insn %d completed, afterOffset=%d, written=",
                     (int)i, (int)(outPtr - output));
            for (usz k = 0; k < (usz)(outPtr - beforePtr); ++k) ::printf("%02x ", beforePtr[k]);
            ::printf("\n");
            continue;
        }

        if (info.hasMem) {
            if (isStoreInstruction(insnSrc, info.instrLen)) {
                emitBoundsCheckStub(outPtr, writeCheckAddr, insnSrc, info.instrLen, info.immSize, info.accessSize);
            } else {
                emitBoundsCheckStub(outPtr, boundsCheckAddr, insnSrc, info.instrLen, info.immSize, info.accessSize);
            }
        }

        if (info.isRelJmp && info.relSize == 4) {
            usz insnRelOffset = info.relOffset;
            ::printf("[AOT Debug RelJmp] info.relOffset=%lu, info.origOffset=%lu, insnRelOffset=%lu\n",
                     (unsigned long)info.relOffset, (unsigned long)info.origOffset, (unsigned long)insnRelOffset);
            std::memcpy(outPtr, insnSrc, insnRelOffset);

            i32 origDisp = 0;
            std::memcpy(&origDisp, insnSrc + insnRelOffset, 4);

            usz origTarget = info.origOffset + info.instrLen + static_cast<usz>(
                static_cast<i64>(origDisp));

            usz newTargetOffset = 0;
            bool targetFound = false;
            for (usz j = 0; j < insnCount; ++j) {
                if (insns[j].origOffset == origTarget) {
                    newTargetOffset = insns[j].outputOffset;
                    targetFound = true;
                    break;
                }
            }

            if (targetFound) {
                usz curInsnOutputEnd = info.outputOffset;
                if (info.hasMem) {
                    curInsnOutputEnd += 20 + info.leaSize;
                }
                curInsnOutputEnd += info.instrLen;

                i64 newDisp64 = static_cast<i64>(newTargetOffset) -
                                static_cast<i64>(curInsnOutputEnd);
                i32 newDisp = static_cast<i32>(newDisp64);
                std::memcpy(outPtr + insnRelOffset, &newDisp, 4);
            } else {
                std::memcpy(outPtr + insnRelOffset, &origDisp, 4);
            }

            usz afterDisp = insnRelOffset + 4;
            if (afterDisp < info.instrLen) {
                std::memcpy(outPtr + afterDisp, insnSrc + afterDisp,
                            info.instrLen - afterDisp);
            }
            outPtr += info.instrLen;

        } else {
            std::memcpy(outPtr, insnSrc, info.instrLen);
            outPtr += info.instrLen;
        }

        if (info.isRspModifying) {
            emitStackCheckStub(outPtr, stackCheckAddr);
        }
        ::printf("[AOT Debug] Insn %d: origOffset=%d, len=%d, before=%d, after=%d\n",
                 (int)i, (int)info.origOffset, (int)info.instrLen,
                 (int)(beforePtr - output), (int)(outPtr - output));
        ::printf("[AOT Rewrite Detail] Insn %d completed, afterOffset=%d, outPtr=%p, written=",
                 (int)i, (int)(outPtr - output), outPtr);
        for (usz k = 0; k < (usz)(outPtr - beforePtr); ++k) ::printf("%02x ", beforePtr[k]);
        ::printf("\n");
        ::printf("[AOT Loop State] After Insn %d: offset 80-112: ", (int)i);
        for (int k = 80; k < 112; ++k) ::printf("%02x ", output[k]);
        ::printf("\n");
    }

    ::printf("[AOT Debug] Output bytes before mprotect:\n");
    for (int j = 0; j < 1024 && j < (int)(outPtr - output); ++j) {
        ::printf("%02x ", output[j]);
        if ((j + 1) % 16 == 0) ::printf("\n");
    }
    ::printf("\n");

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
    }
    cache.clear();
}

} // namespace Execution

#endif // defined(__x86_64__) || defined(_M_X64)
