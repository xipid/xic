# ✦ XiC

![C++](https://img.shields.io/badge/C++-17-blue?logo=cplusplus)
![Python](https://img.shields.io/badge/Python-3.8+-yellow?logo=python)
![WASM](https://img.shields.io/badge/WASM-Ready-purple?logo=webassembly)
![License](https://img.shields.io/badge/License-Apache--2.0-green)

**Xi Core (xic)** is a zero-exception, memory-deterministic C++ framework designed for extreme performance in IoT, mesh-networking (Rho), and real-time distributed systems.

---

## 🚀 Vision

xic provides the building blocks for high-performance applications that need to run anywhere: from embedded **ESP32** microcontrollers to **Linux** servers and **Web** browsers. It prioritizes deterministic memory patterns and zero-copy data flow.

## 📦 Key Components

- **Collections**: High-performance `String` (COW), `Tree` (CSS queries), and `Array` (multi-dimensional views).
- **LLT (Loss-less Transformation)**: High-performance encryption (Monocypher) and compression (ZSTD/LZ4).
- **Execution**: Stackless cooperative `Routines` and high-level `Process` management.
- **Hardware**: Standardized drivers for IMUs, GPS, and environmental sensors with built-in geodetic math.

---

## 🛠️ Usage

### C++ (Embedded/Native)
```cpp
#include <Collection/String.hpp>


Collection::String identity = "xi:node:42";
```

### Python (cppyy-powered)
```python
import xi

identity = xi.String("xi:node:42")
print(f"Node Identity: {identity}")
```

### JavaScript/WASM
```javascript
import { initXi } from 'xic';

const xi = await initXi();
const identity = new xi.String("xi:node:42");
```

---

## 🏗️ Building

xic uses a unified build system powered by `pnpm` and `build.js`.

```bash
pnpm install
pnpm build         # Build all targets (WASM, Python, Docs)
pnpm build:wasm    # Build WASM binaries
pnpm build:python  # Generate Python wheel
pnpm build:docs    # Generate documentation
```

## 📜 License

Distributed under the **Apache-2.0** License. See `LICENSE` for more information.

---

<p align="center">
  Developed with obsession for performance by <b>Xi</b>
</p>
