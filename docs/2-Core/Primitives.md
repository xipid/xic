# Core Primitives

The **Xi::Primitives** header defines the foundational type system and metaprogramming utilities used across the entire framework. It prioritizes cross-platform consistency and memory determinism.

---

## Type System

xic uses a concise, explicit naming convention for integer and floating-point types.

### Integers
| Type | Bits | Description |
| :--- | :--- | :--- |
| `u8`, `i8` | 8 | Unsigned/Signed byte. |
| `u16`, `i16`| 16 | Unsigned/Signed short. |
| `u32`, `i32`| 32 | Unsigned/Signed integer. |
| `u64`, `i64`| 64 | Unsigned/Signed long long. |
| `usz` | Var | Unsigned size type (target-dependent, usually `size_t`). |

### Floating Point
| Type | Bits | Description |
| :--- | :--- | :--- |
| `f32` | 32 | Single-precision float. |
| `f64` | 64 | Double-precision float. |

---

## Metaprogramming Utilities

xic provides lean alternatives to standard template library utilities to keep compile times fast and binaries small.

### Movement and Swapping
- **`Xi::Move(T &&arg)`**: Casts an lvalue to an rvalue (equivalent to `std::move`).
- **`Xi::Swap(T &a, T &b)`**: Efficiently swaps two objects using move semantics.

### SFINAE & Traits
- **`EnableIf<bool, T>`**: Essential for template metaprogramming.
- **`IsSame<U, V>`**: Compile-time check for type identity.
- **`HasSerialize<T>`**: Detects if a class supports xic serialization protocols.

---

## Constants and Common Logic

### Mathematical Constants
- **`PI`**: `3.1415926535...`
- **`E`**: `2.7182818284...`
- **`null`**: A type-safe `nullptr` constant.

### Hashing
The framework includes a high-performance **FNV-1a** hasher for strings and POD types, which powers the internal `Map` and `Tree` query systems.

```cpp
usz h = FNVHasher<const char*>::fnvHash("Hello World");
```

---

## Deterministic Memory Interface

The `IMemoryDevice` interface provides a unified abstraction for allocating memory across different hardware backends (CPU, GPU, or DMA-capable buffers on ESP32).

```cpp
virtual void *alloc(usz size) = 0;
virtual void upload(void *handle, const void *src, usz size) = 0;
```
