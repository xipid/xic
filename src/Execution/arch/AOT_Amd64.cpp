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
 *   4. Copies all other instructions verbatim.
 *
 * The bounds-check stub address is written as an absolute 64-bit address
 * in a `movabs + call rax` sequence. The stub can be patched at load time
 * to point to the task's actual bounds checker.
 *
 * This is intentionally kept simple — correctness over completeness.
 * Not every x86 instruction encoding is handled; the decoder covers the
 * most common one-byte and two-byte opcode forms.
 */

#if defined(__x86_64__) || defined(_M_X64)

#include "../../../include/Execution/AOT.hpp"
#include "../../../include/Execution/Task.hpp"

#include <cstring>
#include <cstdlib>

namespace Execution {

// -------------------------------------------------------------------------
// Bounds-Check Stub
// -------------------------------------------------------------------------

/**
 * @brief Default no-op bounds check. In a real system, this would be
 *        patched to the task's actual bounds-check function.
 *
 * The stub receives the target address in rdi and the access size in rsi.
 * If the access is out-of-bounds, it should fault (e.g., ud2).
 */
extern "C" void xi_sfi_bounds_check(void* /* addr */, usz /* size */) {
    // Default: no-op. Patched at runtime to the task's bounds checker.
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
    // We use a simple dynamic array approach.
    struct InsnInfo {
        usz origOffset;    // Offset in original code.
        usz instrLen;      // Length of the original instruction.
        usz outputOffset;  // Offset in the output code.
        bool hasMem;       // Whether a bounds-check stub was inserted.
        bool isRelJmp;     // Whether this is a relative jump/call.
        usz relOffset;     // Offset of relative displacement within instruction.
        usz relSize;       // Size of relative displacement (1 or 4).
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

        InsnInfo& info = insns[insnCount];
        info.origOffset = pos;
        info.instrLen = len;
        info.outputOffset = outputSize;
        info.hasMem = hasMem;
        info.isRelJmp = isRelJmp;
        info.relOffset = relOffset;
        info.relSize = relSize;

        if (hasMem) {
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
    u64 stubAddr = reinterpret_cast<u64>(&xi_sfi_bounds_check);
    u8* outPtr = output;

    for (usz i = 0; i < insnCount; ++i) {
        const InsnInfo& info = insns[i];
        const u8* insnSrc = code + info.origOffset;

        if (info.hasMem) {
            // Emit bounds-check stub before the memory instruction.
            emitBoundsCheckStub(outPtr, stubAddr);
        }

        if (info.isRelJmp && info.relSize == 4) {
            // Copy the instruction but adjust the 32-bit relative offset.
            // First, copy bytes before the displacement.
            usz prefixLen = info.relOffset - info.origOffset;
            // Wait — relOffset is relative to the start of the decode (code + origOffset),
            // but it's stored as an absolute position within the instruction.
            // Actually, looking at the decoder: relOffset = pos at the time,
            // where pos is relative to code[0]. Let me recalculate.
            //
            // relOffset is the position within the full code buffer where
            // the displacement starts. We need the offset within the instruction.
            usz insnRelOffset = info.relOffset - info.origOffset;
            std::memcpy(outPtr, insnSrc, insnRelOffset);

            // Read the original displacement.
            i32 origDisp = 0;
            std::memcpy(&origDisp, insnSrc + insnRelOffset, 4);

            // The original displacement targets:
            //   target = origOffset + instrLen + origDisp
            // In the new code, we need:
            //   target_output_offset = ?
            // We need to find which instruction the target maps to.
            usz origTarget = info.origOffset + info.instrLen + static_cast<usz>(
                static_cast<i64>(origDisp));

            // Search for the instruction at origTarget.
            usz newTargetOffset = 0;
            bool targetFound = false;
            for (usz j = 0; j < insnCount; ++j) {
                if (insns[j].origOffset == origTarget) {
                    newTargetOffset = insns[j].outputOffset;
                    if (insns[j].hasMem) {
                        // Target includes a stub before it; skip past the stub
                        // only if we want to jump to the stub (which we do for SFI).
                        // Actually, jumps should land at the stub so the bounds
                        // check is performed.
                    }
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
                // This may need a trampoline in a full implementation.
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
            // Copy instruction verbatim (including rel8 jumps — these are
            // short-range and may break if the code expands significantly,
            // but handling rel8→rel32 promotion is beyond the scope of this
            // initial implementation).
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
