# Mathematics and Linear Algebra

The **Xi::Math** module provides high-performance scalar functions, vector mathematics, and matrix transformations optimized for both embedded SIMD and native architectures.

---

## Scalar Functions

xic wraps standard mathematical functions to ensure consistent behavior across platforms.

```cpp
f32 s = Xi::sin(1.5f);
f32 c = Xi::cos(1.5f);
f32 root = Xi::sqrt(64.0f);
```

Common extensions:
- **`sqr(x)`**: Returns $x^2$.
- **`inverse(x)`**: Returns $1/x$.
- **`relu(x)`**: Returns $max(0, x)$.
- **`sigmoid(x)`**: Standard logistic function.

---

## Vector and Matrix Types

The framework includes standard POD structures for linear algebra:
- **`Vector2`, `Vector3`, `Vector4`**: Standard 2D/3D/4D float vectors.
- **`Matrix4`**: A $4 \times 4$ float matrix.
- **`Tensor`**: An alias for `Array<f32>`, used for multi-dimensional data.

### Automatic Overloads
A unique feature of xic math is the automatic overloading of scalar functions for vector types. You can pass a `Vector3` to `sin()` and it will return a `Vector3` with the sine of each component.

```cpp
Vector3 v = {0.0f, PI, PI/2};
Vector3 result = Xi::sin(v); // result is {0.0f, 0.0f, 1.0f}
```

---

## Spatial Transformations

For graphics and hardware positioning, use the built-in matrix generators:

- **`identity()`**: The identity matrix.
- **`translate(x, y, z)`**: Translation matrix.
- **`rotateX/Y/Z(rad)`**: Euler rotations.
- **`lookAt(eye, center, up)`**: View matrix.
- **`perspective(fov, ar, n, f)`**: Projection matrix.

```cpp
Matrix4 view = lookAt({0, 0, 5}, {0, 0, 0}, {0, 1, 0});
```

---

## Data Reductions

Useful for sensors or signal processing:
- **`sum(container)`**: Total sum of elements.
- **`mean(container)`**: Average value.
- **`var(container)`**: Variance.
- **`std(container)`**: Standard deviation.
- **`softmax(container)`**: Normalized exponential probabilities.

---

## Performance Notes

Where possible, loop-heavy functions (like `sum` or `matmul`) are annotated with `omp simd` or use platform-specific intrinsics to leverage SIMD units (like the ESP32-S3's PIE instructions or SSE/AVX on Linux).
