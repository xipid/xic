# XiC: Zero-Exception C++ Core Library 🚀

Welcome to the official technical documentation for **XiC**.

XiC is a lightweight, zero-dependency, and memory-deterministic C++ core library. Engineered to run on everything from resource-constrained microcontrollers (such as ESP32) to full-scale Linux application backends, XiC provides a robust, zero-exception runtime foundation. It enables high-performance application development without the overhead or unpredictability of standard runtime features.

---

## Technical Pillars

1. **Memory-Deterministic Collections**
   - Copy-On-Write Strings: Optimized COW string implementations reducing dynamic allocation overhead.
   - Deterministic Tree Systems: Node-based hierarchical structures queryable via a CSS-like path selector.
   - Universal Data Buffers: Fixed-capacity and dynamically growing buffers with strict boundary checks.
   - Static Mapping: Hash tables and lookup maps tailored for minimal heap fragmentation.

2. **Software Fault Isolation (SFI) Task Sandbox**
   - Sandboxed Task Execution: Ahead-of-Time (AOT) JIT rewriter with Redundant Bounds Check Elimination (BCE) for stack and frame footprint optimizations.
   - Prioritized Swap Paging: Intelligent LRU swap eviction utilizing range-specific page monitoring.
   - Cooperative Yielding & Sleep: Non-blocking task sleep and core yielding during page faults.
   - Fork Bomb Prevention: Resource quota management and cascade teardown for child tasks.
   - W^X Memory Enforcement & Switch Validation: Restricts memory pages to be either writable or executable, sanitizes CPU registers, and validates instruction pointers at scheduler boundaries to prevent escapes.

3. **Professional Terminal & Command Parsing**
   - CLI Command Parser: Zero-allocation command-line argument parsing with nested command definitions.
   - ANSI Formatting Engine: Rich formatting, styling, and colorization for terminals.
   - Interactive Prompts: Non-blocking console inputs, prompt loops, and status outputs.

4. **Encoding, Serialization, and Logging**
   - Zero-Allocation YAML: A stream-based YAML serializer and parser requiring zero dynamic heap operations.
   - Structured Logging: Multi-channel logging with millisecond timestamping and severity filtering.
   - Regex Engine: A deterministic regular expression engine designed for embedded footprints.

5. **Geodetic & Spatial Math**
   - Transformations: High-performance spatial math, quaternions, and geodetic coordinate transformations (ECEF to WGS84).
   - Sensor Drivers: Unified peripheral driver APIs for IMUs, GPS modules, and input devices.

6. **Graphics & Mesh Pipeline**
   - Mesh Engine: Memory-deterministic vertex and mesh management.
   - Camera System: Viewport, projection, and look-at camera matrices for real-time visualization.
   - Window Management: Window hooks, display loops, and inputs.

---

## Navigating the Docs

Use the sidebar navigation to explore:
*   [Getting Started](1-Home/GettingStarted.md) — Set up xic in PlatformIO or custom build environments.
*   [Core Primitives](2-Core/Primitives.md) — Dive into Logic, Math, Time, and Transforms.
*   [Collections](3-Collections/Buffers.md) — Detailed guide on buffers, strings, maps, streams, and trees.
*   [Task Subsystem](8-Task/Architecture.md) — Read about task sandboxing, SFI, swap paging, and scheduling.
*   [System Subsystem](11-System/Processes.md) — Learn how child processes and system utilities interact.
