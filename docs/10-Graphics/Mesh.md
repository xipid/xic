# Meshes and Vertex Geometry

The **Mesh3** class manages the complex geometric data required for 3D rendering. It handles the synchronization between CPU-resident vertex arrays and the high-speed buffers on the GPU.

---

## Mesh Structure

A `Mesh3` consists of a list of **Vertices** and an optional list of **Indices**.

### Vertex Precision
The `Vertex` structure is packed and optimized for modern hardware:
-   **Position** (x, y, z)
-   **UV** (u, v)
-   **Normals** (nx, ny, nz)
-   **Joints** (j[4], w[4]) — For skeletal animation.

```cpp
using namespace Graphics;

Mesh3 *cube = new Mesh3();

// Define a vertex (Position, UV, Normal)
Vertex v = {0.0f, 0.5f, 0.0f,  0.5f, 1.0f,  0,0,1};
cube->vertices.push(v);
```

---

## GPU Synchronization

xic uses a **Dirty Flag** pattern to ensure data is only uploaded to the GPU when it actually changes, minimizing bus traffic.

```cpp
// 1. Modify the vertices array
cube->vertices[0].y = 1.0f;
cube->dirty = true;

// 2. Synchronize with the GPU
cube->upload(); // Frees old buffers and creates new ones
```


---

## Best Practices

1.  **Index Everything**: Always provide an `indices` array. It allows the GPU to reuse vertices, significantly improving rendering speed.
2.  **Manual Upload**: While some high-level APIs might call `upload()` for you, calling it explicitly after a large data change is the safest way to ensure the GPU is ready.
3.  **Ownership**: Meshes are often shared between multiple `Renderable3` nodes. Ensure you manage the lifetime of your `Mesh3` pointer carefully (e.g., using a central resource bank).
4.  **Packing**: The `Vertex` struct uses `#pragma pack(1)`. If you define manual vertex data, ensure it matches this alignment.
