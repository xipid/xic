# Project TODO List

This document tracks upcoming design implementations, performance optimizations, and hardware co-design tasks for the XiC Task subsystem.

---

## 1. Software-Defined Virtual Memory (SDVM) Optimizations

To run processes oblivious to one's memory bounds on MMU-less, cacheless processors (such as clustered RISC-V with local SRAM and high-latency PCIe CXI DRAM), we must optimize the Software Fault Isolation (SFI) translation layer to avoid instruction bloat and pipeline stalls.

- [ ] **Register-Backed Translation Cache (Software TLB)**
  - Reserve two CPU registers (e.g., `t5` for cached Virtual Page Number and `t6` for physical SRAM bank offset).
  - Implement a fast-path register check before loads/stores, reducing translation checks from 10 instructions to 3 instructions on cache hits.
- [ ] **Loop-Invariant Translation & Range Checks (Static BCE)**
  - Extend the AOT rewriter to analyze loop boundaries and validate entire memory buffers (e.g., `array[0...N]`) before entering a loop.
  - Eliminate all SFI check stubs from the loop body, allowing loops to run at 100% native execution speed.
- [ ] **Cooperative Page-Fault Scheduling (`onFetch` Yielding)**
  - Ensure that when [onFetch](file:///home/xi/Repo/xic/include/Task/Task.hpp#L572-L573) is triggered, the microkernel calls [task.stop()](file:///home/xi/Repo/xic/include/Task/Task.hpp#L442) to pause the faulted task.
  - Allow the scheduler to run other ready tasks to mask the millisecond latency of the PCIe CXI page DMA transfer.
- [ ] **Coarse-Grained Contiguous Segments**
  - Implement an optional single-segment base/limit model for tasks that trade memory granularity for a simple 2-instruction SFI overhead check.

---

## 2. Hardware-Assisted SFI (RISC-V Co-Design)

Minimal hardware additions to eliminate AOT bounds stubs entirely and achieve native execution speeds on MMU-less cores.

- [ ] **Base-and-Bounds Hardware Registers**
  - Implement core-local registers: `SFI_BASE` (physical start), `SFI_LIMIT` (size), and `SFI_CONTROL` (enable/disable checks).
  - Perform bounds checks (`vaddr >= BASE && vaddr + size <= LIMIT`) concurrently within the CPU pipeline.
- [ ] **Zero-Bloat JIT compilation**
  - Compile guest memory instructions into standard native instructions, relying on hardware bounds registers to throw a SIGSEGV equivalent on violation.
- [ ] **Microsecond Context Switching & Traps**
  - Update bounds registers inside context switch routines.
  - Trap out-of-bounds violations to trigger fast DMA page loading from CXI DRAM without software validation overhead.
