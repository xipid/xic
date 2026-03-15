#include <Xi/Math.hpp>
#include <Xi/Array.hpp>

namespace Xi {
namespace Math {

Matrix4 identity() {
  Matrix4 r = {{{0}}};
  r.m[0][0] = 1;
  r.m[1][1] = 1;
  r.m[2][2] = 1;
  r.m[3][3] = 1;
  return r;
}

Matrix4 translate(f32 x, f32 y, f32 z) {
  Matrix4 r = identity();
  r.m[3][0] = x;
  r.m[3][1] = y;
  r.m[3][2] = z;
  return r;
}

Matrix4 rotateX(f32 rad) {
  Matrix4 r = identity();
  f32 c = cos(rad), s = sin(rad);
  r.m[1][1] = c;
  r.m[1][2] = s;
  r.m[2][1] = -s;
  r.m[2][2] = c;
  return r;
}

Matrix4 rotateY(f32 rad) {
  Matrix4 r = identity();
  f32 c = cos(rad), s = sin(rad);
  r.m[0][0] = c;
  r.m[0][2] = -s;
  r.m[2][0] = s;
  r.m[2][2] = c;
  return r;
}

Matrix4 rotateZ(f32 rad) {
  Matrix4 r = identity();
  f32 c = cos(rad), s = sin(rad);
  r.m[0][0] = c;
  r.m[0][1] = s;
  r.m[1][0] = -s;
  r.m[1][1] = c;
  return r;
}

Matrix4 lookAt(Vector3 eye, Vector3 center, Vector3 up) {
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
  r.m[1][0] = u.x;
  r.m[2][0] = -f.x;
  r.m[0][1] = s.y;
  r.m[1][1] = u.y;
  r.m[2][1] = -f.y;
  r.m[0][2] = s.z;
  r.m[1][2] = u.z;
  r.m[2][2] = -f.z;
  r.m[3][0] = -dot(s, eye);
  r.m[3][1] = -dot(u, eye);
  r.m[3][2] = dot(f, eye);
  return r;
}

Matrix4 perspective(f32 fov, f32 ar, f32 n, f32 f) {
  f32 thf = tan(fov * 0.5f);
  Matrix4 r = {{{0}}};
  r.m[0][0] = 1.0f / (ar * thf);
  r.m[1][1] = 1.0f / thf;
  r.m[2][2] = f / (f - n);
  r.m[2][3] = 1.0f;
  r.m[3][2] = -(f * n) / (f - n);
  return r;
}

Matrix4 ortho(f32 l, f32 r_, f32 b, f32 t, f32 n, f32 f) {
  Matrix4 r = {{{0}}};
  r.m[0][0] = 2.0f / (r_ - l);
  r.m[1][1] = 2.0f / (t - b);
  r.m[2][2] = 1.0f / (f - n);
  r.m[3][0] = -(r_ + l) / (r_ - l);
  r.m[3][1] = -(t + b) / (t - b);
  r.m[3][2] = -n / (f - n);
  r.m[3][3] = 1.0f;
  return r;
}

Matrix4 transpose(const Matrix4 &m) {
  Matrix4 r;
  for (int i = 0; i < 4; ++i)
    for (int j = 0; j < 4; ++j)
      r.m[i][j] = m.m[j][i];
  return r;
}

Matrix4 multiply(const Matrix4 &a, const Matrix4 &b) {
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

f32 det(const Matrix4 &m) {
  f32 a = m.m[0][0], b = m.m[0][1], c = m.m[0][2], d = m.m[0][3];
  f32 e = m.m[1][0], f = m.m[1][1], g = m.m[1][2], h = m.m[1][3];
  f32 i = m.m[2][0], j = m.m[2][1], k = m.m[2][2], l = m.m[2][3];
  f32 n = m.m[3][0], o = m.m[3][1], p = m.m[3][2], q = m.m[3][3];

  return a * (f * (k * q - l * p) - g * (j * q - l * o) + h * (j * p - k * o)) -
         b * (e * (k * q - l * p) - g * (i * q - l * n) + h * (i * p - k * n)) +
         c * (e * (j * q - l * o) - f * (i * q - l * n) + h * (i * o - j * n)) -
         d * (e * (j * p - k * o) - f * (i * p - k * n) + g * (i * o - j * n));
}

Matrix4 inverse(const Matrix4 &m) {
  f32 d = det(m);
  if (Xi::Math::abs(d) < 1e-8f)
    return identity();
  f32 invDet = 1.0f / d;

  f32 a = m.m[0][0], b = m.m[0][1], c = m.m[0][2], d_ = m.m[0][3];
  f32 e = m.m[1][0], f = m.m[1][1], g = m.m[1][2], h = m.m[1][3];
  f32 i = m.m[2][0], j = m.m[2][1], k = m.m[2][2], l = m.m[2][3];
  f32 n = m.m[3][0], o = m.m[3][1], p = m.m[3][2], q = m.m[3][3];

  Matrix4 res;
  res.m[0][0] = invDet * (f * (k * q - l * p) - g * (j * q - l * o) +
                          h * (j * p - k * o));
  res.m[1][0] = -invDet * (e * (k * q - l * p) - g * (i * q - l * n) +
                           h * (i * p - k * n));
  res.m[2][0] = invDet * (e * (j * q - l * o) - f * (i * q - l * n) +
                          h * (i * o - j * n));
  res.m[3][0] = -invDet * (e * (j * p - k * o) - f * (i * p - k * n) +
                           g * (i * o - j * n));

  res.m[0][1] = -invDet * (b * (k * q - l * p) - c * (j * q - l * o) +
                           d_ * (j * p - k * o));
  res.m[1][1] = invDet * (a * (k * q - l * p) - c * (i * q - l * n) +
                          d_ * (i * p - k * n));
  res.m[2][1] = -invDet * (a * (j * q - l * o) - b * (i * q - l * n) +
                           d_ * (i * o - j * n));
  res.m[3][1] = invDet * (a * (j * p - k * o) - b * (i * p - k * n) +
                          c * (i * o - j * n));

  res.m[0][2] = invDet * (b * (g * q - h * p) - c * (f * q - h * o) +
                          d_ * (f * p - g * o));
  res.m[1][2] = -invDet * (a * (g * q - h * p) - c * (e * q - h * n) +
                           d_ * (e * p - g * n));
  res.m[2][2] = invDet * (a * (f * q - h * o) - b * (e * q - h * n) +
                          d_ * (e * o - f * n));
  res.m[3][2] = -invDet * (a * (f * p - g * o) - b * (e * p - g * n) +
                           c * (e * o - f * n));

  res.m[0][3] = -invDet * (b * (g * l - h * k) - c * (f * l - h * j) +
                           d_ * (f * k - g * j));
  res.m[1][3] = invDet * (a * (g * l - h * k) - c * (e * l - h * i) +
                          d_ * (e * k - g * i));
  res.m[2][3] = -invDet * (a * (f * l - h * j) - b * (e * l - h * i) +
                           d_ * (e * j - f * i));
  res.m[3][3] = invDet * (a * (f * k - g * j) - b * (e * k - g * i) +
                          c * (e * j - f * i));

  return res;
}
f32 sum(const Array<f32> &a) {
    f32 s = 0;
    for (usz i = 0; i < a.fragments.size(); ++i) {
        const auto &f = a.fragments.data()[i];
        const f32 *d = f.data();
        usz n = f.size();
        _Pragma("omp simd") for (usz k = 0; k < n; ++k) s += d[k];
    }
    return s;
}

f32 mean(const Array<f32> &a) {
    usz n = a.size();
    return (n == 0) ? 0 : sum(a) / (f32)n;
}

f32 var(const Array<f32> &a) {
    usz n = a.size();
    if (n == 0) return 0;
    f32 m = mean(a);
    f32 v = 0;
    for (usz i = 0; i < a.fragments.size(); ++i) {
        const auto &f = a.fragments.data()[i];
        const f32 *d = f.data();
        usz count = f.size();
        for (usz k = 0; k < count; ++k) {
            f32 diff = d[k] - m;
            v += diff * diff;
        }
    }
    return v / (f32)n;
}

f32 std(const Array<f32> &a) { return Xi::Math::sqrt(var(a)); }

Array<f32> softmax(const Array<f32> &a) {
    Array<f32> res;
    usz n = a.size();
    res.allocate(n);
    f32 maxVal = -1e30f;
    for (usz i = 0; i < a.fragments.size(); ++i) {
        const auto &f = a.fragments.data()[i];
        const f32 *d = f.data();
        usz count = f.size();
        for (usz k = 0; k < count; ++k)
            if (d[k] > maxVal) maxVal = d[k];
    }
    f32 sumExp = 0;
    for (usz i = 0; i < a.fragments.size(); ++i) {
        const auto &f = a.fragments.data()[i];
        const f32 *d = f.data();
        usz count = f.size();
        usz offset = f.offset;
        for (usz k = 0; k < count; ++k) {
            f32 e = Xi::Math::exp(d[k] - maxVal);
            res[offset + k] = e;
            sumExp += e;
        }
    }
    f32 invSumExp = 1.0f / sumExp;
    for (usz i = 0; i < n; i++) res[i] *= invSumExp;
    return res;
}

} // namespace Math
} // namespace Xi
