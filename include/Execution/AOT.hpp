/**
 * @file AOT.hpp
 * @brief Ahead-of-Time binary rewriter for software fault isolation (SFI).
 *
 * The AOT engine scans native code regions, decodes instructions, and
 * rewrites loads/stores/jumps to enforce memory isolation via
 * Software Fault Isolation (SFI). Each architecture provides its own decoder
 * and rewriter backend.
 */

#ifndef XI_EXECUTION_AOT_HPP
#define XI_EXECUTION_AOT_HPP

#include "../Collection/Array.hpp"
#include "../Xi/Primitives.hpp"

namespace Execution {

using namespace Xi;
using namespace Collection;

struct MemoryRegion; // Forward declaration (defined in Task.hpp).

/**
 * @struct AOTRegion
 * @brief Tracks a region of code that has been AOT-rewritten.
 */
struct AOTRegion {
    usz originalAddr;   ///< Start address of the original code.
    usz originalSize;   ///< Size of the original code region.
    u8* patchedCode;    ///< Heap-allocated buffer with rewritten instructions.
    usz patchedSize;    ///< Size of the patched code.
};

/**
 * @struct AOTResult
 * @brief Result of an AOT rewrite operation.
 */
struct AOTResult {
    u8* patchedCode;    ///< Output buffer with rewritten instructions.
    usz patchedSize;    ///< Size of patched code.
    usz originalSize;   ///< Size of original code consumed.
    bool success;       ///< True if rewrite completed without errors.
};

/**
 * @class AOT
 * @brief Architecture-dispatched AOT binary rewriter.
 *
 * The rewriter performs the following transformations:
 *   1. Decodes each instruction in the input buffer.
 *   2. For loads/stores: inserts bounds-check trampolines that validate
 *      addresses against the task's MemoryRegion list.
 *      - Static addresses (known at rewrite time) are translated directly.
 *      - Dynamic addresses get runtime bounds checks.
 *   3. For jumps/calls:
 *      - Static targets within the AOT'd region → rewrite to patched offset.
 *      - Dynamic/external targets → trampoline that re-AOTs or faults.
 *   4. Emits the rewritten code into a scratch buffer.
 */
class AOT {
public:
    /**
     * @brief Rewrites a code region with SFI instrumentation.
     *
     * @param code       Pointer to the original native code.
     * @param codeSize   Size of the code region in bytes.
     * @param regions    The task's memory region map for bounds checking.
     * @param taskBase   Base address of the task's virtual address space.
     * @return AOTResult containing the patched code buffer.
     */
    static AOTResult rewrite(const u8* code, usz codeSize,
                             const Array<MemoryRegion>& regions,
                             usz taskBase);

    /**
     * @brief Checks if the given address range has a cached AOT rewrite.
     *
     * @param cache  Array of previously rewritten regions to search.
     * @param addr   Start address to look up.
     * @param size   Size of the region.
     * @return Pointer to the cached AOTRegion, or nullptr if not found.
     */
    static AOTRegion* findCached(Array<AOTRegion>& cache, usz addr, usz size);

    /**
     * @brief Invalidates all cached rewrites overlapping with [addr, addr+size).
     *
     * Call this when a task writes into its own code region (JIT),
     * before jumping to the modified code.
     *
     * @param cache  The AOT cache to invalidate within.
     * @param addr   Start address of the invalidated region.
     * @param size   Size of the invalidated region.
     */
    static void invalidate(Array<AOTRegion>& cache, usz addr, usz size);

    /**
     * @brief Frees all patched code buffers in the cache.
     *
     * @param cache  The AOT cache to destroy.
     */
    static void destroyCache(Array<AOTRegion>& cache);

    /**
     * @brief Frees a patched code buffer.
     *
     * @param patchedCode  The buffer to free.
     * @param patchedSize  The size of the patched code.
     */
    static void freePatchedCode(u8* patchedCode, usz patchedSize);
};

} // namespace Execution

#endif // XI_EXECUTION_AOT_HPP
