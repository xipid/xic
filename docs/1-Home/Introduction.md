# Introduction

**xic** is a performance-first C++ framework designed for high-density embedded systems and modern cloud architectures. It prioritizes zero-exception logic, memory determinism, and extreme structural efficiency.

Built to power the next generation of ESP32 and native workloads, xic provides a unified API surface that feels as modern as JavaScript but remains as lean as bare-metal C.

---

## Core Philosophy

### Zero-Exception Architecture
We believe exceptions have no place in mission-critical systems. Every operation in xic is designed to be deterministic. Errors are handled through state checks and return codes, ensuring your application never "explodes" in production.

### Memory Determinism
xic uses custom collection implementations and static buffers where possible. We manage memory with surgical precision, reducing fragmentation and ensuring consistent performance over long uptimes.

### Developer Fluent API
While the internals are optimized for the metal, the external API is designed for speed of development. We use modern C++ patterns—like property-like syntax and CSS-inspired tree selectors—to make your code readable and expressive.

---

## What's Inside

The framework is divided into specialized modules:

1.  **Core**: Time, math, and primitive type abstractions.
2.  **Collections**: High-performance Strings, Arrays, and sophisticated Trees.
3.  **Terminal**: Professional CLI tools with ANSI formatting and progress management.
4.  **LLT (Loss-less Transformation)**: Cryptography and compression utilities.
5.  **Encoding**: Native YAML parsing and structured logging.
6.  **Resource**: Unified filesystem and networking abstractions.
7.  **Execution**: Routines and process management.
8.  **Hardware**: Direct sensor integration and spatial mathematics.
9.  **Graphics**: Mesh-ready rendering and window management.

---

## Getting Help

If you're a developer looking to build robust, lean systems, you've come to the right place. Dive into the **Getting Started** guide to set up your first xic project.
