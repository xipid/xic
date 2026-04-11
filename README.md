# ✦ XiC

![C++](https://img.shields.io/badge/C++-17-blue?logo=cplusplus)
![Python](https://img.shields.io/badge/Python-3.8+-yellow?logo=python)
![WASM](https://img.shields.io/badge/WASM-Ready-purple?logo=webassembly)
![License](https://img.shields.io/badge/License-Apache--2.0-green)

**XiC (Xi Core)** is a professional-grade, zero-exception, and memory-deterministic C++ framework. It is engineered for extreme performance in IoT, and real-time distributed systems.

Built for the next generation of ESP32 and native workloads, XiC provides a unified API surface that feels as modern as JavaScript but remains as lean as bare-metal C.

---

## 💎 Core Philosophy

### Zero-Exception Architecture
We believe exceptions have no place in mission-critical systems. Every operation in XiC is designed to be deterministic. Errors are handled through state checks and return codes, ensuring your application remains stable even in failure states.

### Memory Determinism
XiC uses custom collection implementations and static buffers where possible. We manage memory with surgical precision, significantly reducing fragmentation and ensuring consistent performance over long uptimes.

### Developer Fluent API
While the internals are optimized for the metal, the external API is designed for speed of development. We use modern C++ patterns—like property-like syntax and CSS-inspired tree selectors—to make your code readable and expressive.

---

## 📦 Functional Pillars

| Module | Description |
| :--- | :--- |
| **Collections** | High-performance `String` (COW), `Tree` (CSS queries), and multi-dimensional `Array`. |
| **Terminal** | Professional CLI toolkit with rich ANSI formatting and progress management. |
| **LLT** | Loss-less Transformations: High-performance encryption and compression. |
| **Encoding** | Native high-speed YAML/JSON parsing and structured logging. |
| **Hardware** | Standardized drivers for IMUs, GPS, and sensors with geodetic math. |
| **Graphics** | Mesh-ready 3D rendering pipeline and window management. |
| **Execution** | Stackless cooperative `Routines` and high-level `Process` management. |

---

## 🛠️ Multi-Language Integration

### C++ (Native & Embedded)
```cpp
#include <Collection/String.hpp>

Collection::String identity = "xi:node:42";
```

### Python (Self-Healing Bindings)
```python
import xi

identity = xi.String("xi:node:42")
print(f"Node Identity: {identity}")
```

### JavaScript (WASM Native)
```javascript
import { initXi } from 'xic';

const xi = await initXi();
const identity = new xi.String("xi:node:42");
```

---

## 🏗️ Building

XiC uses a unified build system powered by `pnpm` and `build.js` with automated SDK management.

```bash
pnpm install
pnpm build:wasm    # Automatically provisions Emscripten and builds WASM
pnpm build:python  # Manages .venv and generates Python wheel
pnpm build:docs    # Generates GitBook documentation
```

---

## 📜 License

Distributed under the **Apache-2.0** License. See `LICENSE` for more information.

<p align="center">
  Developed with obsession for performance by <b>Xi</b>
</p>
