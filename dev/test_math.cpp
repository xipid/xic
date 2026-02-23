#include "Xi/Math.hpp"
#include "Xi/Vector.hpp"
#include <iostream>

using namespace Xi;

int main() {
  std::cout << "Testing Math Scalar..." << std::endl;
  std::cout << "sin(pi/2): " << Math::sin(1.570796f) << std::endl;
  std::cout << "rsqrt(4): " << Math::rsqrt(4.0f) << " (expected 0.5)"
            << std::endl;

  std::cout << "Testing Generic Struct Math..." << std::endl;
  Vector3 v1 = {1, 2, 3};
  Vector3 v2 = {4, 5, 6};

  // Automatic support for Vector3 (POD)
  Vector3 v3 = Math::sin(v1);
  std::cout << "sin(v1): " << v3.x << ", " << v3.y << ", " << v3.z << std::endl;

  // Operator Overloads
  Vector3 v_sum = v1 + v2;
  std::cout << "v1 + v2: " << v_sum.x << ", " << v_sum.y << ", " << v_sum.z
            << std::endl;

  std::cout << "Testing Tensor Math..." << std::endl;
  Array<f32> a;
  a.allocate(3);
  a[0] = 1.0f;
  a[1] = 2.0f;
  a[2] = 3.0f;

  // Tensor Op Array
  Array<f32> b = a * 2.0f;
  std::cout << "a * 2.0: ";
  for (usz i = 0; i < b.size(); ++i)
    std::cout << b[i] << " ";
  std::cout << std::endl;

  // Tensor Reduction
  std::cout << "sum(a): " << Math::sum(a) << std::endl;
  std::cout << "mean(a): " << Math::mean(a) << std::endl;
  std::cout << "var(a): " << Math::var(a) << std::endl;

  std::cout << "Testing Array<Vector3> Reduction..." << std::endl;
  Array<Vector3> av;
  av.allocate(2);
  av[0] = {1, 1, 1};
  av[1] = {2, 2, 2};
  std::cout << "sum(Array<Vector3>): " << Math::sum(av) << " (expected 9)"
            << std::endl;
  std::cout << "mean(Array<Vector3>): " << Math::mean(av) << " (expected 1.5)"
            << std::endl;

  std::cout << "Testing Matmul..." << std::endl;
  Array<f32> m1;
  m1.allocate(4);
  m1[0] = 1;
  m1[1] = 2;
  m1[2] = 3;
  m1[3] = 4;
  Array<f32> m2;
  m2.allocate(4);
  m2[0] = 5;
  m2[1] = 6;
  m2[2] = 7;
  m2[3] = 8;
  Array<f32> mr = Math::matmul(m1, m2, 2, 2, 2);
  std::cout << "Matmul result: " << mr[0] << " " << mr[1] << " " << mr[2] << " "
            << mr[3] << std::endl;

  std::cout << "Testing Matrix Transformations..." << std::endl;
  Matrix4 id = Math::identity();
  std::cout << "Identity[0][0]: " << id.m[0][0] << std::endl;

  Matrix4 view = Math::lookAt({0, 0, 5}, {0, 0, 0}, {0, 1, 0});
  std::cout << "View Matrix generated." << std::endl;

  std::cout << "Testing Matrix Det & Inverse..." << std::endl;
  Matrix4 m = id;
  m.m[0][0] = 2.0f;
  std::cout << "Det(m): " << Math::det(m) << " (expected 2)" << std::endl;
  Matrix4 mi = Math::inverse(m);
  std::cout << "Inverse[0][0]: " << mi.m[0][0] << " (expected 0.5)"
            << std::endl;

  return 0;
}
