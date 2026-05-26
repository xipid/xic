# Shaders and Uniform Data

Shaders in xic are modern, data-driven programs that define the visual appearance of rendered objects. They operate in tandem with **ShaderData** to receive transformation matrices and context from the CPU.

---

## The Shader Object

A `Shader` encapsulates the compiled GPU code (compiled via Diligent Engine backends).

---

## Shader Data (Uniforms)

To position an object accurately, the camera passes a **ShaderData** structure to the GPU. This structure contains the essential matrices for vertex transformation.

### ShaderData Structure
-   **mvp**: Model-View-Projection matrix (64 bytes). This transforms local vertex coordinates directly to screen space.
-   **world**: World transformation matrix (64 bytes). Used for lighting calculations that require coordinates in world space.

```cpp
struct ShaderData {
    Matrix4 mvp;
    Matrix4 world;
};
```

---

## Dynamic Type Injection

Shaders are often loaded dynamically through **XiN** or YAML/JSON configurations. Because they are integrated with the xic reflection system, you can swap shaders at runtime by simply updating the `shader` pointer on a `Renderable3`.

```cpp
// Runtime shader swap
player->shader = nightVisionShader;
```

---

## Best Practices

1.  **Alignment**: Ensure your Uniform buffers in HLSL/GLSL match the 16-byte alignment of the `Matrix4` structure.
2.  **Resource Persistence**: Shaders are heavy GPU objects. Load them once and share them across multiple `Renderable3` instances wherever possible.
3.  **MVP Calculation**: The `Camera3` handles the MVP calculation internally during the `render()` pass. You only need to provide the target shader.
