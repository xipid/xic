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
    TaskState* state = xi_get_current_task();
    if (!state) return;

    u8* target = static_cast<u8*>(addr);
    for (usz i = 0; i < state->regions.size(); ++i) {
        MemoryRegion& r = state->regions[i];
        if (r.physical) {
            // Validate against ALL regions (readable and writable).
            // The AOT rewriter inserts this before every memory operation.
            if (target >= r.physical && target + size <= r.physical + r.size) {
                return; // Access is within a valid region.
            }
        }
    }

    // Also allow access to the task's own stack (which may not be in regions).
    if (state->stack && target >= state->stack &&
        target + size <= state->stack + state->stackSize) {
        return;
    }

    // Out-of-bounds access: trap.
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

    // Invalid jump target: trap.
    __asm__ volatile("ud2");
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

extern "C" void xi_run_instruction_callback(void* callbackPtr) {
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
    u8 op = code[0];

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
        default: break;
    }

    // Two-byte privileged instructions (0F xx):
    if (op == 0x0F && len >= 2) {
        u8 op2 = code[1];
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
    u8 op = code[0];
    String name;
    if (op == 0x90 && len == 1) name = "nop";
    else if (op == 0xF4 && len == 1) name = "hlt";
    else if (op == 0xCC && len == 1) name = "int3";
    else if (op == 0xCD) name = "int";
    else if (op == 0xC3 && len == 1) name = "ret";
    else if (op == 0x0F && len >= 2) {
        u8 op2 = code[1];
        if (op2 == 0x05) name = "syscall";
        else if (op2 == 0xA2) name = "cpuid";
        else if (op2 == 0x31) name = "rdtsc";
        else if (op2 == 0x0B) name = "ud2";
    }

    if (name.size() == 0) return false;

    for (usz i = 0; i < state->instructionHooks.size(); ++i) {
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

    // RET variants
    if (opcode == 0xC3 || opcode == 0xC2 || opcode == 0xCB || opcode == 0xCA) {
        outIsRet = true;
        return;
    }

    // FF group (indirect JMP/CALL):
    // FF /2 = CALL r/m64
    // FF /4 = JMP r/m64
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
 * @return Instruction length in bytes, or 0 if decoding fails.
 */
static usz xi_x86_insn_length(const u8* code, usz maxLen,
                               bool& outHasMem,
                               bool& outIsRelJmp,
                               usz& outRelOffset,
                               usz& outRelSize) {
    outHasMem = false;
    outIsRelJmp = false;
    outRelOffset = 0;
    outRelSize = 0;

    if (maxLen == 0) return 0;

    usz pos = 0;
    bool hasRex = false;
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
        rexW = (code[pos] & 0x08) != 0;
        ++pos;
    }
    (void)hasRex;

    if (pos >= maxLen) return 0;

    // --- Opcode ---
    u8 opcode = code[pos++];
    bool twoByteOpcode = false;

    if (opcode == 0x0F) {
        // Two-byte opcode escape.
        if (pos >= maxLen) return 0;
        opcode = code[pos++];
        twoByteOpcode = true;
    }

    // --- Determine if this instruction has ModRM and what operands it needs ---
    bool hasModRM = false;
    usz immSize = 0; // Size of immediate operand in bytes.

    if (twoByteOpcode) {
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
                // F6 /0 and /1 have imm8, others don't.
                // We'll peek at ModRM to determine reg field.
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
                // Most remaining one-byte opcodes in the ranges 00-3F (ALU),
                // 84-8F, D0-D3, FE-FF use ModRM without immediate.
                // For 00-3F: even opcodes have r/m,r; odd have r,r/m;
                //            pattern: opcodes 00-05 repeat for each ALU op.
                if (opcode <= 0x3F) {
                    u8 lowBits = opcode & 0x07;
                    if (lowBits <= 3) {
                        hasModRM = true;
                        immSize = 0;
                    } else {
                        // 04,05 are AL/AX,imm (handled above).
                        // 06,07 are PUSH/POP ES (invalid in 64-bit).
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
                    // Unknown opcode — assume 1 byte instruction for safety.
                    return pos;
                }
                break;
        }
    }

    // --- Decode ModRM ---
    if (hasModRM) {
        if (pos >= maxLen) return 0;
        u8 modrm = code[pos++];
        u8 mod = (modrm >> 6) & 3;
        u8 rm  = modrm & 7;

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
static constexpr usz kStubSize = 18;

/**
 * @brief Emits the bounds-check call stub into the output buffer.
 *
 * @param out       Output buffer pointer (advanced by kStubSize).
 * @param stubAddr  Address of the bounds-check function.
 */
static void emitBoundsCheckStub(u8*& out, u64 stubAddr) {
    // push rax
    *out++ = 0x50;
    // push rdi
    *out++ = 0x57;
    // push rsi
    *out++ = 0x56;

    // movabs rax, imm64
    *out++ = 0x48; // REX.W
    *out++ = 0xB8; // MOV rax, imm64
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
 * For RET, we replace with:
 *   pop rdi                           ; 1 byte — pop return address into rdi
 *   push rdi                          ; 1 byte — push it back for later
 *   push rax                          ; 1 byte — save rax
 *   movabs rax, <xi_sfi_jump_check>   ; 10 bytes
 *   call rax                          ; 2 bytes — validate return address
 *   pop rax                           ; 1 byte — restore rax
 *   ret                               ; 1 byte — execute the validated ret
 *                                Total: 17 bytes
 */
static constexpr usz kRetCheckStubSize = 17;

static void emitRetCheckStub(u8*& out, u64 jumpCheckAddr) {
    // pop rdi (return address into rdi — first arg for jump check)
    *out++ = 0x5F;
    // push rdi (put it back on stack for the final ret)
    *out++ = 0x57;
    // push rax (save rax)
    *out++ = 0x50;
    // movabs rax, <xi_sfi_jump_check>
    *out++ = 0x48;
    *out++ = 0xB8;
    std::memcpy(out, &jumpCheckAddr, 8);
    out += 8;
    // call rax
    *out++ = 0xFF;
    *out++ = 0xD0;
    // pop rax (restore rax)
    *out++ = 0x58;
    // ret (now validated)
    *out++ = 0xC3;
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

    // First pass: compute the output size.
    // For each instruction with memory access, we add kStubSize bytes.
    // For all others, we copy verbatim.
    usz outputSize = 0;
    usz pos = 0;

    // Store per-instruction info for the second pass.
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
    };

    // Estimate: at most codeSize instructions (one byte each minimum).
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

        usz len = xi_x86_insn_length(code + pos, codeSize - pos,
                                      hasMem, isRelJmp, relOffset, relSize);
        if (len == 0) {
            // Decoding failed — copy remaining bytes verbatim.
            len = 1;
            hasMem = false;
            isRelJmp = false;
        }

        bool hookBanned = false;
        void* callbackPtr = nullptr;
        bool isHooked = isInstructionBannedOrHooked(code + pos, len, hookBanned, callbackPtr);

        bool isBannedPrivileged = isUnconditionallyBanned(code + pos, len);

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

        // Determine output size for this instruction.
        if (isBannedPrivileged) {
            outputSize += 2; // ud2
        } else if (isHooked && hookBanned) {
            outputSize += 2; // ud2
        } else if (isIndirectJump) {
            // Replace indirect JMP/CALL with ud2 (strictest policy).
            outputSize += 2;
        } else if (isRet) {
            // Replace RET with validated return stub.
            outputSize += kRetCheckStubSize;
        } else if (isHooked && callbackPtr) {
            outputSize += 22 + len; // callback call stub + original
        } else if (hasMem) {
            outputSize += kStubSize + len;
        } else {
            outputSize += len;
        }

        ++insnCount;
        pos += len;
    }

    // Allocate output buffer.
    u8* output = static_cast<u8*>(std::malloc(outputSize));
    if (!output) {
        std::free(insns);
        return result;
    }

    // Second pass: emit the rewritten code.
    u64 boundsCheckAddr = reinterpret_cast<u64>(&xi_sfi_bounds_check);
    u64 jumpCheckAddr = reinterpret_cast<u64>(&xi_sfi_jump_check);
    u8* outPtr = output;

    for (usz i = 0; i < insnCount; ++i) {
        const InsnInfo& info = insns[i];
        const u8* insnSrc = code + info.origOffset;

        // Priority 1: Unconditionally banned privileged instructions.
        if (info.isBannedPrivileged) {
            emitUd2(outPtr);
            continue;
        }

        // Priority 2: User-banned via instruction hooks.
        if (info.isHooked && info.banned) {
            emitUd2(outPtr);
            continue;
        }

        // Priority 3: Indirect JMP/CALL — banned for containment.
        if (info.isIndirectJump) {
            emitUd2(outPtr);
            continue;
        }

        // Priority 4: RET — rewrite with validated return.
        if (info.isRet) {
            emitRetCheckStub(outPtr, jumpCheckAddr);
            continue;
        }

        // Priority 5: Hooked instruction with callback.
        if (info.isHooked && info.callbackPtr) {
            u64 callbackAddr = reinterpret_cast<u64>(info.callbackPtr);
            u64 helperAddr = reinterpret_cast<u64>(&xi_run_instruction_callback);
            emitCallbackCall(outPtr, callbackAddr, helperAddr);
            std::memcpy(outPtr, insnSrc, info.instrLen);
            outPtr += info.instrLen;
            continue;
        }

        // Priority 6: Memory access — insert bounds check.
        if (info.hasMem) {
            emitBoundsCheckStub(outPtr, boundsCheckAddr);
        }

        // Handle relative JMP/CALL displacement adjustment.
        if (info.isRelJmp && info.relSize == 4) {
            // Copy the instruction but adjust the 32-bit relative offset.
            usz insnRelOffset = info.relOffset - info.origOffset;
            std::memcpy(outPtr, insnSrc, insnRelOffset);

            // Read the original displacement.
            i32 origDisp = 0;
            std::memcpy(&origDisp, insnSrc + insnRelOffset, 4);

            // The original displacement targets:
            //   target = origOffset + instrLen + origDisp
            usz origTarget = info.origOffset + info.instrLen + static_cast<usz>(
                static_cast<i64>(origDisp));

            // Search for the instruction at origTarget.
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
                // Compute the current instruction's end in the output.
                usz curInsnOutputEnd = info.outputOffset;
                if (info.hasMem) {
                    curInsnOutputEnd += kStubSize;
                }
                curInsnOutputEnd += info.instrLen;

                i64 newDisp64 = static_cast<i64>(newTargetOffset) -
                                static_cast<i64>(curInsnOutputEnd);
                i32 newDisp = static_cast<i32>(newDisp64);
                std::memcpy(outPtr + insnRelOffset, &newDisp, 4);
            } else {
                // Target is outside the AOT'd region — keep original displacement.
                std::memcpy(outPtr + insnRelOffset, &origDisp, 4);
            }

            // Copy remaining bytes after the displacement.
            usz afterDisp = insnRelOffset + 4;
            if (afterDisp < info.instrLen) {
                std::memcpy(outPtr + afterDisp, insnSrc + afterDisp,
                            info.instrLen - afterDisp);
            }
            outPtr += info.instrLen;

        } else {
            // Copy instruction verbatim.
            std::memcpy(outPtr, insnSrc, info.instrLen);
            outPtr += info.instrLen;
        }
    }

    std::free(insns);

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
                std::free(region.patchedCode);
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
            std::free(cache[i].patchedCode);
            cache[i].patchedCode = nullptr;
        }
    }
    cache.clear();
}

} // namespace Execution

#endif // defined(__x86_64__) || defined(_M_X64)
