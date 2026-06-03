/**
 * @file Math.hpp
 * @brief High-performance math utilities and linear algebra for the Xi
 * framework.

 */

#ifndef XI_CORE_MATH_HPP
#define XI_CORE_MATH_HPP

#include "../Collection/Array.hpp"
#include "Primitives.hpp"

using namespace Collection;

namespace Xi {

/**
 * @struct Vector2
 * @brief Simple 2D vector.
 */
struct Vector2 {
  f32 x, y;
};

/**
 * @struct Vector3
 * @brief Simple 3D vector.
 */
struct Vector3 {
  f32 x, y, z;
};

/**
 * @struct Vector4
 * @brief Simple 4D vector.
 */
struct Vector4 {
  f32 x, y, z, w;
};

/**
 * @struct Matrix4
 * @brief Standard 4x4 matrix for graphics and physics.
 */
struct Matrix4 {
  f32 m[4][4];
};

/**
 * @typedef Tensor
 * @brief Alias for an array of floats, used for general mathematical tensors.
 */
using Tensor = Array<f32>;

// --- Helper for Automatic Struct Support ---

/** @brief Reinterprets a POD struct as a float pointer. */
template <typename T> inline f32 *as_f32(T &v) {
  return reinterpret_cast<f32 *>(&v);
}
/** @brief Reinterprets a const POD struct as a float pointer. */
template <typename T> inline const f32 *as_f32(const T &v) {
  return reinterpret_cast<const f32 *>(&v);
}
/** @brief Counts the number of f32 elements in a POD struct. */
template <typename T> constexpr usz count_f32() {
  return sizeof(T) / sizeof(f32);
}

// --- Scalar Base Functions ---

#if defined(_MSC_VER) && !defined(__clang__)
#include <math.h>
#define SC_W(name, func) \
  inline f32 name(f32 x) { return func(x); }
SC_W(sin, ::sinf)
SC_W(cos, ::cosf)
SC_W(tan, ::tanf)
SC_W(asin, ::asinf)
SC_W(acos, ::acosf)
SC_W(atan, ::atanf)
SC_W(sinh, ::sinhf)
SC_W(cosh, ::coshf)
SC_W(tanh, ::tanhf)
SC_W(asinh, ::asinhf)
SC_W(acosh, ::acoshf)
SC_W(atanh, ::atanhf)
SC_W(exp, ::expf)
SC_W(log, ::logf)
SC_W(log10, ::log10f)
SC_W(log2, ::log2f)
SC_W(sqrt, ::sqrtf)

inline f32 atan2(f32 y, f32 x) { return ::atan2f(y, x); }
inline f32 sqr(f32 x) { return x * x; }
inline i32 floor(f32 x) { return (i32)::floorf(x); }
inline i32 ceil(f32 x) { return (i32)::ceilf(x); }
inline i32 round(f32 x) { return (i32)::roundf(x); }
inline f32 floor(f32 x, int) { return ::floorf(x); }
inline f32 ceil(f32 x, int) { return ::ceilf(x); }
inline f32 round(f32 x, int) { return ::roundf(x); }
inline f32 abs(f32 x) { return ::fabsf(x); }
inline f32 sgn(f32 x) {
  return (x > 0.0f) ? 1.0f : ((x < 0.0f) ? -1.0f : 0.0f);
}
inline f32 min(f32 a, f32 b) { return (a < b) ? a : b; }
inline f32 max(f32 a, f32 b) { return (a > b) ? a : b; }
inline f32 clamp(f32 v, f32 mn, f32 mx) { return min(max(v, mn), mx); }
inline f32 pow(f32 b, f32 e) { return ::powf(b, e); }
inline f32 inverse(f32 x) { return 1.0f / x; }
inline f32 relu(f32 x) { return max(0.0f, x); }
inline f32 sigmoid(f32 x) { return 1.0f / (1.0f + ::expf(-x)); }
inline f32 rsqrt(f32 x) { return 1.0f / ::sqrtf(x); }
#else
#define SC_W(name, func)                                                       \
  inline f32 name(f32 x) { return func(x); }

SC_W(sin, __builtin_sinf)
SC_W(cos, __builtin_cosf)
SC_W(tan, __builtin_tanf)
SC_W(asin, __builtin_asinf)
SC_W(acos, __builtin_acosf)
SC_W(atan, __builtin_atanf)
SC_W(sinh, __builtin_sinhf)
SC_W(cosh, __builtin_coshf)
SC_W(tanh, __builtin_tanhf)
SC_W(asinh, __builtin_asinhf)
SC_W(acosh, __builtin_acoshf)
SC_W(atanh, __builtin_atanhf)
SC_W(exp, __builtin_expf)
SC_W(log, __builtin_logf)
SC_W(log10, __builtin_log10f)
SC_W(log2, __builtin_log2f)
SC_W(sqrt, __builtin_sqrtf)

inline f32 atan2(f32 y, f32 x) { return __builtin_atan2f(y, x); }
inline f32 sqr(f32 x) { return x * x; }
inline i32 floor(f32 x) { return (i32)__builtin_floorf(x); }
inline i32 ceil(f32 x) { return (i32)__builtin_ceilf(x); }
inline i32 round(f32 x) { return (i32)__builtin_roundf(x); }
inline f32 floor(f32 x, int) { return __builtin_floorf(x); }
inline f32 ceil(f32 x, int) { return __builtin_ceilf(x); }
inline f32 round(f32 x, int) { return __builtin_roundf(x); }
inline f32 abs(f32 x) { return __builtin_fabsf(x); }
inline f32 sgn(f32 x) {
  return (x > 0.0f) ? 1.0f : ((x < 0.0f) ? -1.0f : 0.0f);
}
inline f32 min(f32 a, f32 b) { return (a < b) ? a : b; }
inline f32 max(f32 a, f32 b) { return (a > b) ? a : b; }
inline f32 clamp(f32 v, f32 mn, f32 mx) { return min(max(v, mn), mx); }
inline f32 pow(f32 b, f32 e) { return __builtin_powf(b, e); }
inline f32 inverse(f32 x) { return 1.0f / x; }
inline f32 relu(f32 x) { return max(0.0f, x); }
inline f32 sigmoid(f32 x) { return 1.0f / (1.0f + __builtin_expf(-x)); }
inline f32 rsqrt(f32 x) { return 1.0f / __builtin_sqrtf(x); }
#endif

// --- Generic Automatic Struct/Vector Overloads ---

#define MATH_FUNC(name)                                                        \
  template <typename T> inline T name(const T &v) {                            \
    T res = v;                                                                 \
    f32 *pr = reinterpret_cast<f32 *>(&res);                                   \
    const f32 *pv = reinterpret_cast<const f32 *>(&v);                         \
    for (usz i = 0; i < sizeof(T) / sizeof(f32); ++i)                          \
      pr[i] = Xi::name(pv[i]);                                                 \
    return res;                                                                \
  }

MATH_FUNC(sin)
MATH_FUNC(cos)
MATH_FUNC(tan)
MATH_FUNC(asin)
MATH_FUNC(acos)
MATH_FUNC(atan)
MATH_FUNC(sinh)
MATH_FUNC(cosh)
MATH_FUNC(tanh)
MATH_FUNC(asinh)
MATH_FUNC(acosh)
MATH_FUNC(atanh)
MATH_FUNC(exp)
MATH_FUNC(log)
MATH_FUNC(log10)
MATH_FUNC(log2)
MATH_FUNC(sqrt)
MATH_FUNC(sqr)
MATH_FUNC(abs)
MATH_FUNC(sgn)
MATH_FUNC(inverse)
MATH_FUNC(relu)
MATH_FUNC(sigmoid)
MATH_FUNC(rsqrt)

// --- Reductions ---

/** @brief Sum of elements in a POD struct. */
template <typename T> inline f32 sum(const T &v) {
  const f32 *p = reinterpret_cast<const f32 *>(&v);
  f32 s = 0;
  for (usz i = 0; i < sizeof(T) / sizeof(f32); ++i)
    s += p[i];
  return s;
}

/** @brief Mean of elements in a POD struct. */
template <typename T> inline f32 mean(const T &v) {
  return sum(v) / (f32)(sizeof(T) / sizeof(f32));
}

/** @brief Sum of elements in an Array. */
template <typename T> f32 sum(const Array<T> &a) {
  f32 s = 0;
  usz n = a.size();
  const T *d = a.data();
  _Pragma("omp simd") for (usz i = 0; i < n; ++i) s += Xi::sum(d[i]);
  return s;
}

/** @brief Mean of elements in an Array. */
template <typename T> f32 mean(const Array<T> &a) {
  usz n = a.size();
  return (n == 0) ? 0 : sum(a) / (f32)n;
}

/** @brief Variance of elements in an Array. */
template <typename T> f32 var(const Array<T> &a) {
  usz n = a.size();
  if (n == 0)
    return 0;
  f32 m = mean(a);
  f32 v = 0;
  const T *d = a.data();
  for (usz i = 0; i < n; ++i) {
    f32 diff = (f32)d[i] - m;
    v += diff * diff;
  }
  return v / (f32)n;
}

/** @brief Standard deviation of elements in an Array. */
template <typename T> f32 std(const Array<T> &a) { return Xi::sqrt(var(a)); }

// Explicit specializations for Array<f32> (Tensor)
template <> f32 sum<f32>(const Array<f32> &a);
template <> f32 mean<f32>(const Array<f32> &a);
template <> f32 var<f32>(const Array<f32> &a);
template <> f32 std<f32>(const Array<f32> &a);

// --- Tensor (Element-wise) ---

#define TS_W(name)                                                             \
  template <typename T> Array<T> name(const Array<T> &a) {                     \
    Array<T> res;                                                              \
    res.allocate(a.size());                                                    \
    T *pr = res.data();                                                        \
    const T *pa = a.data();                                                    \
    usz n = a.size();                                                          \
    _Pragma("omp simd") for (usz i = 0; i < n; ++i) pr[i] = Xi::name(pa[i]);   \
    return res;                                                                \
  }

TS_W(sin)
TS_W(cos)
TS_W(tan)
TS_W(asin)
TS_W(acos)
TS_W(atan)
TS_W(exp)
TS_W(log)
TS_W(sqrt)
TS_W(sqr)
TS_W(abs)
TS_W(relu)
TS_W(sigmoid)
TS_W(rsqrt)

/** @brief Softmax activation function. */
template <typename Arr> Arr softmax(const Arr &a) {
  Arr res;
  usz n = a.size();
  res.allocate(n);
  const auto *d = a.data();
  auto *r = res.data();
  f32 maxVal = -1e30f;
  for (usz i = 0; i < n; i++)
    if ((f32)d[i] > maxVal)
      maxVal = (f32)d[i];
  f32 sumExp = 0;
  for (usz i = 0; i < n; i++) {
    r[i] = Xi::exp((f32)d[i] - maxVal);
    sumExp += (f32)r[i];
  }
  f32 invSumExp = 1.0f / sumExp;
  for (usz i = 0; i < n; i++)
    r[i] *= invSumExp;
  return res;
}

// Explicit specialization for Array<f32> (Tensor)
template <> Array<f32> softmax<Array<f32>>(const Array<f32> &a);

// --- Matrix/Vector Linear Algebra ---

/** @brief Dot product of two 3D vectors. */
inline f32 dot(Vector3 a, Vector3 b) {
  return a.x * b.x + a.y * b.y + a.z * b.z;
}

/** @brief Generic dot product for arrays or containers. */
template <typename Arr> f32 dot(const Arr &a, const Arr &b) {
  f32 res = 0;
  usz n = a.size() < b.size() ? a.size() : b.size();
  for (usz i = 0; i < n; ++i)
    res += (f32)a[i] * (f32)b[i];
  return res;
}

// --- Matrix Transformations (Free Functions) ---

Matrix4 identity();
Matrix4 translate(f32 x, f32 y, f32 z);
Matrix4 rotateX(f32 rad);
Matrix4 rotateY(f32 rad);
Matrix4 rotateZ(f32 rad);
Matrix4 lookAt(Vector3 eye, Vector3 center, Vector3 up);
Matrix4 perspective(f32 fov, f32 ar, f32 n, f32 f);
Matrix4 ortho(f32 l, f32 r_, f32 b, f32 t, f32 n, f32 f);
Matrix4 transpose(const Matrix4 &m);

/** @brief High-performance matrix multiplication. */
template <typename Arr>
Arr matmul(const Arr &a, const Arr &b, usz M, usz N, usz P) {
  Arr res;
  res.allocate(M * P);
  for (usz i = 0; i < M * P; ++i)
    res[i] = 0;
  for (usz i = 0; i < M; ++i) {
    for (usz k = 0; k < N; ++k) {
      f32 aik = (f32)a[i * N + k];
      _Pragma("omp simd") for (usz j = 0; j < P; ++j) res[i * P + j] +=
          aik * (f32)b[k * P + j];
    }
  }
  return res;
}

Matrix4 multiply(const Matrix4 &a, const Matrix4 &b);
f32 det(const Matrix4 &m);
Matrix4 inverse(const Matrix4 &m);

// --- Vector Operators ---

inline Vector2 operator+(Vector2 a, Vector2 b) {
  return {a.x + b.x, a.y + b.y};
}
inline Vector2 operator-(Vector2 a, Vector2 b) {
  return {a.x - b.x, a.y - b.y};
}
inline Vector2 operator*(Vector2 a, f32 s) { return {a.x * s, a.y * s}; }
inline Vector3 operator+(Vector3 a, Vector3 b) {
  return {a.x + b.x, a.y + b.y, a.z + b.z};
}
inline Vector3 operator-(Vector3 a, Vector3 b) {
  return {a.x - b.x, a.y - b.y, a.z - b.z};
}
inline Vector3 operator*(Vector3 a, f32 s) {
  return {a.x * s, a.y * s, a.z * s};
}
inline Vector4 operator+(Vector4 a, Vector4 b) {
  return {a.x + b.x, a.y + b.y, a.z + b.z, a.w + b.w};
}
inline Vector4 operator-(Vector4 a, Vector4 b) {
  return {a.x - b.x, a.y - b.y, a.z - b.z, a.w - b.w};
}
inline Vector4 operator*(Vector4 a, f32 s) {
  return {a.x * s, a.y * s, a.z * s, a.w * s};
}

/** @brief Matrix multiplication operator. */
inline Matrix4 operator*(const Matrix4 &a, const Matrix4 &b) {
  return multiply(a, b);
}

} // namespace Xi

#endif // XI_CORE_MATH_HPP