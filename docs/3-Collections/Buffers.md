# Buffers and Arrays

The xic framework provides two primary contiguous memory containers: **InlineArray** (the low-level engine) and **Array** (the high-level sparse and multi-dimensional interface).

---

## InlineArray: The Contiguous Core

`InlineArray<T>` is a reference-counted, Copy-on-Write (COW) dynamic array. It is the foundation for `String` and many other internal structures.

### Key Features
-   **Reference Counting**: Multiple arrays can share the same memory block until a modification occurs.
-   **Device Support**: InlineArrays can represent memory resident on a different device (e.g., GPU memory or DMA buffers).
-   **Slicing**: You can create sub-views (slices) of an array without copying the data.

```cpp
InlineArray<u8> buffer;
buffer.allocate(1024);

// Create a view of bytes 10-20
InlineArray<u8> slice = buffer.begin(10, 20);
```

---

## Array: Sparse and Multi-dimensional

`Array<T>` builds upon `InlineArray` to provide a more flexible, potentially non-contiguous container.

### Sparse Storage
An `Array` can consist of multiple "fragments." This allows you to store data at index `0` and index `1,000,000` without allocating the space in between.

```cpp
Array<int> sparse;
sparse[0] = 10;
sparse[1000] = 20; // Only two small fragments allocated
```

### Multidimensional Views
Arrays support expressive N-dimensional shapes.

```cpp
Array<f32> matrix;
matrix.allocate(16);
auto m4x4 = matrix.view(4, 4);

m4x4[1][2] = 5.0f; // Accesses index (1 * 4) + 2
```

---

## Hardware Integration

One of xic's most powerful features is the ability to seamlessly move data between the CPU and hardware devices via the `IMemoryDevice` interface.

```cpp
// Transfer an array to a GPU/DSP device
Array<f32> gpuData = cpuData.to(gpuDevice);

// Access a handle for the device
void* handle = gpuData.deviceView();
```

---

## Comparisons

| Feature | InlineArray | Array |
| :--- | :--- | :--- |
| **Contiguous** | Always | Can be fragmented (Sparse) |
| **Indexing** | Relative to slice | Absolute (Global) |
| **COW** | Built-in | Managed per-fragment |
| **Typical Use** | Strings, small buffers | Tensors, Mesh data, Large datasets |

---

## Best Practices

1.  **Prefer `reserve()`**: If you know the final size, call `reserve()` before pushing elements to minimize reallocations.
2.  **Flattening**: If you need to pass a sparse `Array` to a legacy C function, call `.data()`. This will automatically collapse all fragments into a single contiguous block.
3.  **Move Semantics**: Always use `Xi::Move` when transferring large arrays to avoid triggering a COW copy.
