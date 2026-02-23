#ifndef XI_MATH_HPP
#define XI_MATH_HPP

#include "Primitives.hpp"
#include <cmath>

namespace Xi {
// Forward declarations
template <typename T> class Array;

// --- Simple POD Structs ---
struct Vector2 {
  f32 x, y;
};
struct Vector3 {
  f32 x, y, z;
};
struct Vector4 {
  f32 x, y, z, w;
};
struct Matrix4 {
  f32 m[4][4];
};

using Tensor = Array<f32>;

namespace Math {
// --- Helper for Automatic Struct Support ---
// Treat any POD struct as a contiguous array of f32 for element-wise ops
template <typename T> inline f32 *as_f32(T &v) {
  return reinterpret_cast<f32 *>(&v);
}

template <typename T> inline const f32 *as_f32(const T &v) {
  return reinterpret_cast<const f32 *>(&v);
}

template <typename T> constexpr usz count_f32() {
  return sizeof(T) / sizeof(f32);
}

// --- Scalar Base Functions ---
#define SC_W(name, func)                                                       \
  inline f32 name(f32 x) { return func(x); }

SC_W(sin, __builtin_sinf)
SC_W(cos, __builtin_cosf)
SC_W(tan, __builtin_tanf)
SC_W(asin, __builtin_asinf) SC_W(acos, __builtin_acosf)
    SC_W(atan, __builtin_atanf) SC_W(sinh, __builtin_sinhf)
        SC_W(cosh, __builtin_coshf) SC_W(tanh, __builtin_tanhf)
            SC_W(asinh, __builtin_asinhf) SC_W(acosh, __builtin_acoshf)
                SC_W(atanh, __builtin_atanhf) SC_W(exp, __builtin_expf)
                    SC_W(log, __builtin_logf) SC_W(log10, __builtin_log10f)
                        SC_W(log2, __builtin_log2f)
                            SC_W(sqrt, __builtin_sqrtf) inline f32
    atan2(f32 y, f32 x) {
  return __builtin_atan2f(y, x);
}
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

// --- Generic Automatic Struct/Vector Overloads ---
#define MATH_FUNC(name)                                                        \
  template <typename T> inline T name(const T &v) {                            \
    T res = v;                                                                 \
    f32 *pr = reinterpret_cast<f32 *>(&res);                                   \
    const f32 *pv = reinterpret_cast<const f32 *>(&v);                         \
    for (usz i = 0; i < sizeof(T) / sizeof(f32); ++i)                          \
      pr[i] = Xi::Math::name(pv[i]);                                           \
    return res;                                                                \
  }

MATH_FUNC(sin)
MATH_FUNC(cos)
MATH_FUNC(tan)
MATH_FUNC(asin) MATH_FUNC(acos) MATH_FUNC(atan) MATH_FUNC(sinh) MATH_FUNC(cosh)
    MATH_FUNC(tanh) MATH_FUNC(asinh) MATH_FUNC(acosh) MATH_FUNC(atanh)
        MATH_FUNC(exp) MATH_FUNC(log) MATH_FUNC(log10) MATH_FUNC(log2)
            MATH_FUNC(sqrt) MATH_FUNC(sqr) MATH_FUNC(abs) MATH_FUNC(sgn)
                MATH_FUNC(inverse) MATH_FUNC(relu) MATH_FUNC(sigmoid)
                    MATH_FUNC(rsqrt)

    // --- Reductions ---
    // POD Reduction (only for types that are not Arrays)
    template <typename T>
    inline f32 sum(const T &v) {
  const f32 *p = reinterpret_cast<const f32 *>(&v);
  f32 s = 0;
  for (usz i = 0; i < sizeof(T) / sizeof(f32); ++i)
    s += p[i];
  return s;
}

template <typename T> inline f32 mean(const T &v) {
  return sum(v) / (f32)(sizeof(T) / sizeof(f32));
}

// Array Reductions (overloads for Array and InlineArray)
template <typename T> f32 sum(const Array<T> &a) {
  f32 res = 0;
  usz n = a.size();
  usz members = sizeof(T) / sizeof(f32);
  for (usz i = 0; i < n; ++i) {
    const f32 *p = reinterpret_cast<const f32 *>(&a[i]);
    _Pragma("omp simd") for (usz k = 0; k < members; ++k) res += p[k];
  }
  return res;
}

template <typename T> f32 mean(const Array<T> &a) {
  usz n = a.size();
  if (n == 0)
    return 0;
  usz total_scalars = n * (sizeof(T) / sizeof(f32));
  return sum(a) / (f32)total_scalars;
}

template <typename T> f32 var(const Array<T> &a) {
  usz n = a.size();
  usz members = sizeof(T) / sizeof(f32);
  usz total_scalars = n * members;
  if (total_scalars < 2)
    return 0;

  f32 s = 0;
  f32 s2 = 0;
  for (usz i = 0; i < n; ++i) {
    const f32 *p = reinterpret_cast<const f32 *>(&a[i]);
    _Pragma("omp simd") for (usz k = 0; k < members; ++k) {
      f32 x = p[k];
      s += x;
      s2 += x * x;
    }
  }

  f32 inv_n = 1.0f / (f32)total_scalars;
  f32 m = s * inv_n;
  return (s2 * inv_n) - (m * m);
}

template <typename T> f32 std(const Array<T> &a) {
  return Xi::Math::sqrt(var(a));
}

// --- Tensor (Element-wise) ---
#define TS_W(name)                                                             \
  template <typename T> Array<T> name(const Array<T> &a) {                     \
    Array<T> res;                                                              \
    res.allocate(a.size());                                                    \
    T *pr = res.data();                                                        \
    const T *pa = a.data();                                                    \
    usz n = a.size();                                                          \
    _Pragma("omp simd") for (usz i = 0; i < n; ++i) pr[i] =                    \
        Xi::Math::name(pa[i]);                                                 \
    return res;                                                                \
  }

TS_W(sin)
TS_W(cos)
TS_W(tan)
TS_W(asin) TS_W(acos) TS_W(atan) TS_W(exp) TS_W(log) TS_W(sqrt) TS_W(sqr)
    TS_W(abs) TS_W(relu) TS_W(sigmoid) TS_W(rsqrt)

        template <typename Arr>
        Arr softmax(const Arr &a) {
  Arr res = a;
  usz n = a.size();
  if (n == 0)
    return res;
  f32 m = -1e30f;
  for (usz i = 0; i < n; ++i)
    if ((f32)a[i] > m)
      m = (f32)a[i];
  f32 s = 0;
  for (usz i = 0; i < n; ++i) {
    res[i] = Xi::Math::exp((f32)a[i] - m);
    s += (f32)res[i];
  }
  for (usz i = 0; i < n; ++i)
    res[i] /= s;
  return res;
}

// --- Matrix/Vector Linear Algebra ---
inline f32 dot(Vector3 a, Vector3 b) {
  return a.x * b.x + a.y * b.y + a.z * b.z;
}

template <typename Arr> f32 dot(const Arr &a, const Arr &b) {
  f32 res = 0;
  usz n = a.size() < b.size() ? a.size() : b.size();
  for (usz i = 0; i < n; ++i)
    res += (f32)a[i] * (f32)b[i];
  return res;
}

// --- Matrix Transformations (Free Functions) ---
inline Matrix4 identity() {
  Matrix4 r = {{{0}}};
  r.m[0][0] = 1;
  r.m[1][1] = 1;
  r.m[2][2] = 1;
  r.m[3][3] = 1;
  return r;
}

inline Matrix4 translate(f32 x, f32 y, f32 z) {
  Matrix4 r = identity();
  r.m[3][0] = x;
  r.m[3][1] = y;
  r.m[3][2] = z;
  return r;
}

inline Matrix4 rotateX(f32 rad) {
  Matrix4 r = identity();
  f32 c = cos(rad), s = sin(rad);
  r.m[1][1] = c;
  r.m[1][2] = s;
  r.m[2][1] = -s;
  r.m[2][2] = c;
  return r;
}

inline Matrix4 rotateY(f32 rad) {
  Matrix4 r = identity();
  f32 c = cos(rad), s = sin(rad);
  r.m[0][0] = c;
  r.m[0][2] = -s;
  r.m[2][0] = s;
  r.m[2][2] = c;
  return r;
}

inline Matrix4 rotateZ(f32 rad) {
  Matrix4 r = identity();
  f32 c = cos(rad), s = sin(rad);
  r.m[0][0] = c;
  r.m[0][1] = s;
  r.m[1][0] = -s;
  r.m[1][1] = c;
  return r;
}

inline Matrix4 lookAt(Vector3 eye, Vector3 center, Vector3 up) {
  auto norm = [](Vector3 v) {
    f32 l = Xi::Math::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
    return (l == 0) ? Vector3{0, 0, 0} : Vector3{v.x / l, v.y / l, v.z / l};
  };
  auto cross = [](Vector3 a, Vector3 b) {
    return Vector3{a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z,
                   a.x * b.y - a.y * b.x};
  };
  Vector3 f = norm({center.x - eye.x, center.y - eye.y, center.z - eye.z});
  Vector3 s = norm(cross(f, up));
  Vector3 u = cross(s, f);
  Matrix4 r = identity();
  r.m[0][0] = s.x;
  r.m[1][0] = s.y;
  r.m[2][0] = s.z;
  r.m[0][1] = u.x;
  r.m[1][1] = u.y;
  r.m[2][1] = u.z;
  r.m[0][2] = -f.x;
  r.m[1][2] = -f.y;
  r.m[2][2] = -f.z;
  r.m[3][0] = -dot(s, eye);
  r.m[3][1] = -dot(u, eye);
  r.m[3][2] = dot(f, eye);
  return r;
}

inline Matrix4 perspective(f32 fov, f32 ar, f32 n, f32 f) {
  f32 thf = tan(fov / 2.0f);
  Matrix4 r = {{{0}}};
  r.m[0][0] = 1.0f / (ar * thf);
  r.m[1][1] = 1.0f / thf;
  r.m[2][2] = -(f + n) / (f - n);
  r.m[2][3] = -1.0f;
  r.m[3][2] = -(2.0f * f * n) / (f - n);
  return r;
}

template <typename Arr>
Arr matmul(const Arr &a, const Arr &b, usz M, usz N, usz P) {
  Arr res;
  res.allocate(M * P);
  for (usz i = 0; i < M * P; ++i)
    res[i] = 0;
  // Optimized loop order: i, k, j for linear memory access (Cache Locality)
  for (usz i = 0; i < M; ++i) {
    for (usz k = 0; k < N; ++k) {
      f32 aik = (f32)a[i * N + k];
      _Pragma("omp simd") for (usz j = 0; j < P; ++j) {
        res[i * P + j] += aik * (f32)b[k * P + j];
      }
    }
  }
  return res;
}

inline Matrix4 multiply(const Matrix4 &a, const Matrix4 &b) {
  Matrix4 r = {{{0}}};
  for (int i = 0; i < 4; ++i) {
    for (int k = 0; k < 4; ++k) {
      f32 aik = a.m[i][k];
      _Pragma("omp simd") for (int j = 0; j < 4; ++j) {
        r.m[i][j] += aik * b.m[k][j];
      }
    }
  }
  return r;
}
} // namespace Math

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

// Matrix Operator
inline Matrix4 operator*(const Matrix4 &a, const Matrix4 &b) {
  return Math::multiply(a, b);
}
} // namespace Xi

#endif