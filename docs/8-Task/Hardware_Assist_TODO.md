# TODO: Hardware-Assisted SFI Bounds Verification

This document outlines the design for introducing minimal hardware assistance to the Software Fault Isolation (SFI) Task sandbox. The goal is to completely eliminate software bounds-checking stubs, reduce JIT cache code bloat, and achieve near-native hardware speed on MMU-less processors (such as the proposed 128-core RISC-V clustered SRAM architecture).

---

## 1. The Bottleneck: Software Bounds Checking
Currently, the Ahead-of-Time (AOT) JIT rewriter instrumenting the sandbox must inject bounds-checking stubs before every memory load and store instruction:
* **Code Bloat**: Each bounds check stub is approximately 62 bytes (saving volatile registers, restoring RSP, constructing the target address with `LEA`, performing stack alignment, and calling the C++ handler).
* **Runtime Overhead**: Saving/restoring CPU state and jumping to validation subroutines on every memory access adds instruction pipeline bubbles and cache pressure.

---

## 2. The Solution: Base-and-Bounds Hardware Registers
Instead of implementing a full Memory Management Unit (MMU) with Translation Lookaside Buffers (TLBs) and page-table walking hardware (which are silicon-heavy and consume significant power), we propose a minimal **Base-and-Bounds Register** block per CPU core.

### Hardware Specification
* **`SFI_BASE` Register**: A 64-bit register holding the physical base address of the task's allowed memory bank.
* **`SFI_LIMIT` Register**: A 64-bit register holding the size/limit of the task's allowed memory space.
* **`SFI_CONTROL` Register**: Enables or disables hardware checks (disabled in kernel mode, enabled in sandboxed task mode).

```
                      Guest Load/Store Address (vaddr)
                                     │
                        ┌────────────┴────────────┐
                        ▼                         ▼
                  vaddr >= BASE?            vaddr + size <= LIMIT?
                        │                         │
                  ┌─────┴─────┐             ┌─────┴─────┐
                  │           │             │           │
                 YES          NO           YES          NO
                  │           │             │           │
                  └─────┬─────┘             └─────┬─────┘
                        ▼                         ▼
                 Allow Access                Hardware Trap (SIGSEGV)
```

---

## 3. Co-Design Benefits

### A. Zero JIT Code Bloat
The JIT compiler/rewriter no longer needs to prepend memory accesses with stubs. It compiles:
```assembly
# Original Guest Code
lw a0, 0(s1)
```
Directly into a native load, since the CPU hardware performs the range validation concurrently in the execution pipeline stage.

### B. Microsecond Context Switching
During scheduler task switches:
1. The kernel updates the active task's bounds in `SFI_BASE` and `SFI_LIMIT`.
2. The kernel flips the enable bit in `SFI_CONTROL`.
3. The CPU runs the sandboxed code at 100% native speed.
4. If a task attempts to access an address outside `[BASE, LIMIT)`, the hardware raises a fast exception trap. The kernel intercepts this trap, suspends the task, initiates a Compute Express Link (CXI) DMA transfer from 1TB DRAM to the SRAM bank, and yields the core to another ready task.
