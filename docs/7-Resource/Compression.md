# Resource: Compression

The **Resource::Compression** module provides a unified interface for various lossless compression algorithms. It is designed to be pluggable, allowing you to choose the best balance between speed, memory usage, and compression ratio.

---

## Supported Backends

xic supports three major backends, conditionally enabled based on your build flags:

-   **DEFLATE (`miniz`)**: The standard balance. Very portable and compatible with ZIP/GZIP formats.
-   **LZ4**: Focused on extreme speed. Ideal for real-time data streams and low-latency networking.
-   **ZSTD (Zstandard)**: State-of-the-art compression. Offers excellent ratios and features like dictionary training.

---

## Abstract Interface

All compression engines share the same basic API, allowing you to switch algorithms with a single line of code.

```cpp
using namespace Resource;

// Instantiate your backend (e.g., ZSTD)
ZSTD engine;
engine.level = 3;

// Compress
String compressed = engine.compress("Raw data to be shrunken");

// Decompress
String raw = engine.decompress(compressed);
```

---

## Dictionary Training (ZSTD)

One of xic's specialized features is **ZSTD dictionary training**. By training on a set of common samples (like JSON headers or repeating structured logs), you can drastically improve the compression ratio for small messages.

```cpp
ZSTD engine;

// Train from a sample of messages
Array<String> samples = {"...", "...", "..."};
engine.train(samples, 112 * 1024); // 112KB dictionary

// Subsequent compression calls will now use this learned dictionary
String tiny = engine.compress(newSmallMessage);
```

---

## Memory Management

Embedded systems are memory-constrained. xic allows you to cap the amount of working memory ("scratch space") used by the compression engines.

```cpp
engine.maxScratch = 64 * 1024; // Cap at 64KB for tight ESP32 stacks
```

---

## Best Practices

1.  **Level Selection**: For real-time telemetry, use `LZ4` or a low-level `ZSTD`. For archival storage, use high-level `DEFLATE` or `ZSTD`.
2.  **Context Reuse**: Backends like `ZSTD` reuse internal contexts across calls. Reuse the same engine instance for multiple messages rather than recreating it to avoid high allocation overhead.
3.  **Check Size**: Always check the size of the returned `String`. If compression fails (due to memory or data issues), an empty string is returned.
