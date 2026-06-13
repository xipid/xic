# XiC
> **The Zero-Exception C++ Core Library designed for embedded systems and real-time workloads.**

---

XiC is a lightweight, zero-dependency, and memory-deterministic C++ core library. Engineered to run on everything from resource-constrained microcontrollers (such as ESP32) to full-scale Linux application backends, XiC provides a robust, zero-exception runtime foundation. It enables high-performance application development without the overhead or unpredictability of standard runtime features.

---

## Key Pillars

### 1. Memory-Deterministic Collections
*   **Copy-On-Write Strings:** Optimized COW string implementations that reduce dynamic allocations and page-copy overheads.
*   **Deterministic Tree Systems:** Node-based hierarchical structures queryable via a CSS-like path selector.
*   **Universal Data Buffers:** Fixed-capacity and dynamically growing buffers with strict boundary checks.
*   **Static Mapping:** Hash tables and lookup maps tailored for minimal heap fragmentation.

### 2. Software Fault Isolation (SFI) Task Sandbox
*   **Sandboxed Task Execution:** Ahead-of-Time (AOT) JIT rewriter with Redundant Bounds Check Elimination (BCE) for stack and frame footprint optimizations.
*   **Prioritized Swap Paging:** Intelligent LRU swap eviction utilizing range-specific page monitoring.
*   **Cooperative Yielding & Sleep:** Non-blocking task sleep and core yielding during page faults.
*   **Fork Bomb Prevention:** Resource quota management and cascade teardown for child tasks.
*   **W^X Memory Enforcement & Switch Validation:** Restricts memory pages to be either writable or executable, sanitizes CPU registers, and validates instruction pointers at scheduler boundaries to prevent escapes.

### 3. Professional Terminal & Command Parsing
*   **CLI Command Parser:** Zero-allocation command-line argument parsing with nested command definitions.
*   **ANSI Formatting Engine:** Rich formatting, styling, and colorization for terminals.
*   **Interactive Prompts:** Non-blocking console inputs, prompt loops, and status outputs.

### 4. Encoding, Serialization, and Logging
*   **Zero-Allocation YAML:** A stream-based YAML serializer and parser requiring zero dynamic heap operations.
*   **Structured Logging:** Multi-channel logging with millisecond timestamping and severity filtering.
*   **Regex Engine:** A deterministic regular expression engine designed for embedded footprints.

### 5. Geodetic & Spatial Math
*   **Transformations:** High-performance spatial math, quaternions, and geodetic coordinate transformations (ECEF to WGS84).
*   **Sensor Drivers:** Unified peripheral driver APIs for IMUs, GPS modules, and input devices.

### 6. Graphics & Mesh Pipeline
*   **Mesh Engine:** Memory-deterministic vertex and mesh management.
*   **Camera System:** Viewport, projection, and look-at camera matrices for real-time visualization.
*   **Window Management:** Window hooks, display loops, and inputs.

---

## Getting Started

XiC compiles into a single static library. If you are using PlatformIO (for ESP32 and embedded ecosystems), add the library dependency to your configuration:

```ini
lib_deps =
    xipid/xic
```

### 1. Unified SFI Task Execution
```cpp
#include <Task/Task.hpp>
#include <cstdio>

using namespace Task;

void task_payload(void* arg) {
    std::printf("Hello from sandboxed task context!\n");
}

int main() {
    Task root = Task::root();
    Task child = root.spawn(task_payload, nullptr);
    
    // Enforce 5000 microsecond quota for starvation prevention
    child.setQuota(5000);
    child.resume();
    
    Task::yield();
    return 0;
}
```

### 2. Zero-Allocation CLI Parsing
```cpp
#include <Terminal/Cli.hpp>
#include <cstdio>

int main(int argc, char** argv) {
    Terminal::CliParser parser;
    parser.addOption("port", 'p', "Serial connection port");
    parser.addOption("baud", 'b', "Baud rate (default: 115200)");
    
    if (parser.parse(argc, argv)) {
        auto port = parser.getOption("port");
        std::printf("Connecting to port: %s\n", port.c_str());
    }
    return 0;
}
```

---

## Project Structure

```
├── include/            # Public headers
│   ├── Collection/     # Memory-deterministic collections
│   ├── Encoding/       # YAML, logging, and regex
│   ├── Task/           # SFI Tasks and context switching
│   ├── System/         # Child processes and system APIs
│   ├── Hardware/       # Driver interfaces and spatial math
│   ├── Graphics/       # Mesh and camera systems
│   └── Terminal/       # CLI and interactive prompting
├── src/                # Library source implementations
├── tests/              # Comprehensive test suites
└── docs/               # Technical whitepapers and documentation
```

---

## License

XiC is distributed under the Apache-2.0 License. See `LICENSE` for more information.
