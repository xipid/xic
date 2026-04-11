# Associative Maps

The **Collection::Map** is a high-performance hash map implementation designed for speed and memory efficiency. It utilizes **Robin Hood hashing** with linear probing to minimize lookup variance and cache misses.

---

## Technical Design

### Robin Hood Hashing
Unlike standard linear probing, Robin Hood hashing reorders elements during insertion to ensure that the "distance from home" (PSL - Probe Sequence Length) is balanced across all keys. This prevents "clumping" and ensures predictable lookup times even at high load factors.

### FNV-1a Hashing
The map uses the Fowler-Noll-Vo hash algorithm, which is remarkably fast for the small keys (like strings or integers) typically found in embedded and system applications.

---

## Basic Usage

The API is designed for minimal friction, supporting both explicit methods and subscript operators.

```cpp
Map<String, int> scores;

// Insertion
scores.set("Alice", 100);
scores["Bob"] = 85;

// Retrieval
if (scores.has("Alice")) {
    int val = *scores.get("Alice");
}

// Removal
scores.remove("Bob");
```

---

## Deterministic Serialization

A unique feature of xic Maps is their deterministic serialization. When converted to a string or YAML, the map automatically sorts its keys. This ensures that the resulting data is stable and bit-perfect for hashing or version control comparisons.

```cpp
String data = scores.serialize(); // Keys are sorted: "Alice", "Bob"...
```

---

## Advanced Features

### Iteration
You can iterate over a map using standard C++ range-based for loops. Only occupied buckets are visited.

```cpp
for (auto &entry : scores) {
    printf("Key: %s, Value: %d\n", entry.key.c_str(), entry.value);
}
```

### Reference Counting
Because the map uses an `InlineArray` for its internal bucket storage, it naturally supports reference counting/COW. Moving a map is a constant-time operation.

---

## Performance Thresholds

-   **Initial Capacity**: 16 buckets.
-   **Load Factor**: Resizes at 85% occupancy.
-   **Lookup Complexity**: $O(1)$ average, with very low variance thanks to Robin Hood rebalancing.

---

## Best Practices

1.  **Pointers for Retrieval**: The `get()` method returns a pointer. Always check for `nullptr` before dereferencing if you aren't certain the key exists.
2.  **Custom Key Types**: If using a custom struct as a key, you must provide a specialization for `Xi::FNVHasher` and `Xi::Equal`.
3.  **Memory Overhead**: The map prioritizes speed. If you are extremely memory-constrained, consider if a sorted `Array` with binary search might be more appropriate.
