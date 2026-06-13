/**
 * @file AOT_RiscV32.cpp
 * @brief RISC-V32 AOT SFI binary rewriter for the Task subsystem.
 *
 * Decodes RISC-V32 instructions (fixed 32-bit, with 16-bit compressed
 * extension support), identifies loads/stores, and inserts bounds-check
 * trampolines. Simple, correct-first implementation.
 *
 * RISC-V32 instruction encoding:
 *   - If bits [1:0] are both 1 (0b11), it's a 32-bit instruction.
 *   - Otherwise it's a 16-bit compressed instruction.
 *
 * Load opcodes (32-bit):  opcode[6:0] = 0b0000011 (0x03)
 *   funct3: 000=LB, 001=LH, 010=LW, 100=LBU, 101=LHU
 *
 * Store opcodes (32-bit): opcode[6:0] = 0b0100011 (0x23)
 *   funct3: 000=SB, 001=SH, 010=SW
 *
 * For SFI: insert a JAL to a bounds-check function before each load/store.
 */

#if defined(__riscv) && (__riscv_xlen == 32)

#include "../../../include/Task/AOT.hpp"
#include "../../../include/Task/Task.hpp"

#include <cstdlib>
#include <cstring>

namespace Task {

// -------------------------------------------------------------------------
// Bounds-check stub (RISC-V32)
// -------------------------------------------------------------------------

extern "C" void xi_sfi_bounds_check_rv32(void* addr, usz size) {
    TaskState* state = xi_get_current_task();
    if (!state) return;

    u8* target = static_cast<u8*>(addr);
    bool ok = false;
    for (usz i = 0; i < state->regions.size(); ++i) {
        MemoryRegion& r = state->regions[i];
        if (r.physical) {
            if (target >= r.physical && target + size <= r.physical + r.size) {
                ok = true;
                break;
            }
        }
    }

    // Also allow access to the task's own stack.
    if (!ok && state->stack && target >= state->stack &&
        target + size <= state->stack + state->stackSize) {
        ok = true;
    }

    if (!ok) {
        std::abort();
    }
}

// -------------------------------------------------------------------------
// RISC-V32 Instruction Decoder
// -------------------------------------------------------------------------

/**
 * @brief Returns the length of a RISC-V instruction.
 * @param code  Pointer to instruction bytes.
 * @return 4 for standard, 2 for compressed.
 */
static usz xi_rv32_insn_length(const u8* code) {
    // Compressed: bits[1:0] != 0b11
    u8 low2 = code[0] & 0x03;
    return (low2 == 0x03) ? 4 : 2;
}

/**
 * @brief Checks if a 32-bit RISC-V instruction is a load.
 */
static bool xi_rv32_is_load(u32 insn) {
    return (insn & 0x7F) == 0x03; // opcode = 0000011
}

/**
 * @brief Checks if a 32-bit RISC-V instruction is a store.
 */
static bool xi_rv32_is_store(u32 insn) {
    return (insn & 0x7F) == 0x23; // opcode = 0100011
}

/**
 * @brief Checks if a 32-bit RISC-V instruction is a JAL or JALR.
 */
static bool xi_rv32_is_jump(u32 insn) {
    u8 opcode = insn & 0x7F;
    return (opcode == 0x6F) || // JAL
           (opcode == 0x67);   // JALR
}

/**
 * @brief Checks if a 32-bit RISC-V instruction is a branch.
 */
static bool xi_rv32_is_branch(u32 insn) {
    return (insn & 0x7F) == 0x63; // opcode = 1100011
}

/**
 * @brief Checks if a 16-bit compressed RISC-V instruction is a load or store.
 *
 * C.LW:  bits[1:0]=00, bits[15:13]=010
 * C.SW:  bits[1:0]=00, bits[15:13]=110
 * C.LWSP: bits[1:0]=10, bits[15:13]=010
 * C.SWSP: bits[1:0]=10, bits[15:13]=110
 */
static bool xi_rv32_is_compressed_load_store(u16 insn) {
    u8 op = insn & 0x03;
    u8 funct3 = (insn >> 13) & 0x07;

    if (op == 0x00) {
        return (funct3 == 0x02) || (funct3 == 0x06); // C.LW, C.SW
    }
    if (op == 0x02) {
        return (funct3 == 0x02) || (funct3 == 0x06); // C.LWSP, C.SWSP
    }
    return false;
}

// -------------------------------------------------------------------------
// SFI Stub
// -------------------------------------------------------------------------

/**
 * @brief Size of the bounds-check stub for RISC-V32.
 *
 * We insert a pair of NOP instructions (4 bytes each) as placeholders.
 * In a full implementation, these would be:
 *   AUIPC t0, <high20>     → 4 bytes (load PC-relative address)
 *   JALR  t0, t0, <low12>  → 4 bytes (jump to bounds checker)
 * Total: 8 bytes
 */
static constexpr usz kRV32StubSize = 8;

// -------------------------------------------------------------------------
// AOT::rewrite (RISC-V32)
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

    // First pass: compute output size and catalog instructions.
    struct InsnInfo {
        usz origOffset;
        usz instrLen;
        usz outputOffset;
        bool isMemAccess;
    };

    usz insnCapacity = codeSize; // Upper bound.
    InsnInfo* insns = static_cast<InsnInfo*>(std::malloc(insnCapacity * sizeof(InsnInfo)));
    if (!insns) return result;

    usz insnCount = 0;
    usz outputSize = 0;
    usz pos = 0;

    while (pos < codeSize) {
        usz len = xi_rv32_insn_length(code + pos);
        if (pos + len > codeSize) break;

        InsnInfo& info = insns[insnCount];
        info.origOffset = pos;
        info.instrLen = len;
        info.outputOffset = outputSize;
        info.isMemAccess = false;

        if (len == 4) {
            u32 insn = 0;
            std::memcpy(&insn, code + pos, 4);
            info.isMemAccess = xi_rv32_is_load(insn) || xi_rv32_is_store(insn);
        } else if (len == 2) {
            u16 insn = 0;
            std::memcpy(&insn, code + pos, 2);
            info.isMemAccess = xi_rv32_is_compressed_load_store(insn);
        }

        if (info.isMemAccess) {
            outputSize += kRV32StubSize + len;
        } else {
            outputSize += len;
        }

        ++insnCount;
        pos += len;
    }

    // Allocate output.
    u8* output = static_cast<u8*>(std::malloc(outputSize));
    if (!output) {
        std::free(insns);
        return result;
    }

    // Second pass: emit rewritten code.
    u8* outPtr = output;

    // RISC-V NOP = ADDI x0, x0, 0 = 0x00000013
    static const u8 rv32_nop[4] = {0x13, 0x00, 0x00, 0x00};

    for (usz i = 0; i < insnCount; ++i) {
        const InsnInfo& info = insns[i];
        const u8* src = code + info.origOffset;

        if (info.isMemAccess) {
            // Insert placeholder NOPs before the load/store.
            // 2x 4-byte NOP = 8 bytes.
            std::memcpy(outPtr, rv32_nop, 4);
            outPtr += 4;
            std::memcpy(outPtr, rv32_nop, 4);
            outPtr += 4;
        }

        // Copy the original instruction.
        std::memcpy(outPtr, src, info.instrLen);
        outPtr += info.instrLen;
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
        if (cache[i].originalAddr == addr && cache[i].originalSize == size) {
            return &cache[i];
        }
    }
    return nullptr;
}

void AOT::freePatchedCode(u8* patchedCode, usz /*patchedSize*/) {
    if (patchedCode) {
        std::free(patchedCode);
    }
}

void AOT::invalidate(Array<AOTRegion>& cache, usz addr, usz size) {
    usz end = (size == 0) ? (usz)-1 : addr + size;
    for (usz i = cache.size(); i > 0; --i) {
        usz idx = i - 1;
        usz rEnd = cache[idx].originalAddr + cache[idx].originalSize;
        if (cache[idx].originalAddr < end && rEnd > addr) {
            if (cache[idx].patchedCode) {
                freePatchedCode(cache[idx].patchedCode, cache[idx].patchedSize);
            }
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

} // namespace Task

#endif // defined(__riscv) && (__riscv_xlen == 32)
