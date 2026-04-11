# Logic and Functional Utilities

The **Xi** core provides advanced functional wrappers and pseudo-random number generation (PRNG) utilities designed for low-memory environments.

---

## Type-Erased Callables

The `Xi::Func<T>` class is a lightweight, zero-exception alternative to `std::function`.

### Small Object Optimization (SBO)
To avoid heap fragmentation, `Func` uses an internal buffer of 128 bytes. Any lambda or functor smaller than this is stored directly within the object, ensuring zero allocation during construction.

```cpp
// A simple lambda
Func<void(int)> logValue = [](int x) {
    printf("Value: %d\n", x);
};

// Invocation
logValue(42);
```

### Key Features
- **Efficiency**: No heap allocation for standard lambdas.
- **Portability**: Works flawlessly on ESP32 without requiring RTTI or exceptions.
- **Safety**: Use `isValid()` or the `bool` operator to check if a function is assigned.

---

## Random Number Generation

The **Xi::Random** module provides a suite of pseudo-random generators suitable for simulations, games, and non-cryptographic security.

### Initialization
Seed the generator using system entropy or a specific value:

```cpp
// Seed from system noise
randomSeed();

// Seed from a string hash (deterministic per string)
randomSeed("my-unique-key");
```

### Basic Usage
- **`random(max)`**: Returns `[0, max)`.
- **`random(min, max)`**: Returns `[min, max]`.
- **`randomFloat()`**: Returns a float between `0.0` and `1.0`.

### Secure Entropy
For sensitive operations, `secureRandomFill` utilizes hardware-backed entropy (where available, like the ESP32's HWRNG) or cryptographically secure PRNG algorithms.

```cpp
String key;
secureRandomFill(key, 32); // 32 bytes of secure random data
```

---

## Best Practices

1.  **Prefer SBO**: Keep your lambda captures small (under 128 bytes) to stay on the stack.
2.  **Explicit Seed**: In embedded systems, always call `randomSeed()` once at startup to avoid predictable random sequences.
3.  **Check Validity**: Always verify your `Func` objects before calling them, especially if they are passed as callbacks.
