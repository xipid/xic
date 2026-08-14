#include <Hardware/Spatial.hpp>

namespace Xi {

HardwareSpatial &HardwareSpatial::getInstance() {
  static HardwareSpatial instance;
  return instance;
}

void HardwareSpatial::update() {
  u64 nowNS = getSystemTimeNS();
  u64 nowUS = nowNS / 1000ULL;
  
  // Update time and delta in TimedTransformMeasurement
  sync(nowUS);

  // 1. Update all hardware devices
  for (usz i = 0; i < mpu.length(); ++i)
    mpu[i]->update();
  for (usz i = 0; i < gps.length(); ++i)
    gps[i]->update();
  for (usz i = 0; i < dht.length(); ++i) {
    dht[i]->update();
    // Sync temperature/humidity to the Environmental base class
    temperature = dht[i]->temperature;
    humidity = dht[i]->humidity;
  }

  // 2. Time Synchronization
  syncTime(nowNS);

  // 3. Sensor Fusion & Fault Tolerance
  fuseSensors(dt);

  // 4. Update Geospatial
  if (gps.length() > 0 && gps[0]->hasFix) {
    setGeoPos(gps[0]->pos);
  }
}

HardwareSpatial::HardwareSpatial() {
  q0 = 1.0f;
  q1 = 0.0f;
  q2 = 0.0f;
  q3 = 0.0f;
  sync(getSystemTimeNS() / 1000ULL);
}

u64 HardwareSpatial::getSystemTimeNS() {
#if defined(ARDUINO)
  return (u64)micros() * 1000ULL;
#else
  return 0; // Fallback for simulation
#endif
}

void HardwareSpatial::syncTime(u64 nowSys) {
  realTime = nowSys + timeOffsetNS;

  // If GPS has a fix and provides a time, we could calculate timeOffsetNS
  for (usz i = 0; i < gps.length(); ++i) {
    if (gps[i]->hasFix && gps[i]->lastTimeSync > 0) {
      // timeOffsetNS = gps[i]->lastTimeSync - nowSys;
      // break;
    }
  }
}

void HardwareSpatial::fuseSensors(float deltaT) {
  if (mpu.length() == 0)
    return;

  // Simplistic fault tolerance: find first responding MPU
  MPUDevice *activeMPU = nullptr;
  for (usz i = 0; i < mpu.length(); ++i) {
    if (Xi::abs(mpu[i]->accel.x) > 0.001f ||
        Xi::abs(mpu[i]->accel.z) > 0.001f) {
      activeMPU = mpu[i];
      break;
    }
  }

  if (!activeMPU)
    return;

  // Mahony Fusion
  Vector3 a = activeMPU->accel;
  Vector3 g = activeMPU->gyro * (3.14159265f / 180.0f); // to rad/s

  // Gravity projection to find "Down"
  // In the body frame, the gravity vector is the 3rd row of the rotation matrix (transposed)
  down.x = 2.0f * (q1 * q3 - q0 * q2);
  down.y = 2.0f * (q0 * q1 + q2 * q3);
  down.z = q0 * q0 - q1 * q1 - q2 * q2 + q3 * q3;

  // Update deltaPos (linear acceleration excluding gravity)
  deltaPos.x = a.x - down.x;
  deltaPos.y = a.y - down.y;
  deltaPos.z = a.z - down.z;

  // Mahony algorithm
  float recipNorm = 1.0f / Xi::sqrt(a.x * a.x + a.y * a.y + a.z * a.z);
  a.x *= recipNorm;
  a.y *= recipNorm;
  a.z *= recipNorm;

  float halfvx = q1 * q3 - q0 * q2;
  float halfvy = q0 * q1 + q2 * q3;
  float halfvz = q0 * q0 - 0.5f + q3 * q3;

  float halfex = (a.y * halfvz - a.z * halfvy);
  float halfey = (a.z * halfvx - a.x * halfvz);
  float halfez = (a.x * halfvy - a.y * halfvx);

  float Ki = 0.005f, Kp = 2.0f;
  eInt[0] += halfex * deltaT;
  eInt[1] += halfey * deltaT;
  eInt[2] += halfez * deltaT;
  g.x += Kp * halfex + Ki * eInt[0];
  g.y += Kp * halfey + Ki * eInt[1];
  g.z += Kp * halfez + Ki * eInt[2];

  g.x *= (0.5f * deltaT);
  g.y *= (0.5f * deltaT);
  g.z *= (0.5f * deltaT);
  float qa = q0, qb = q1, qc = q2;
  q0 += (-qb * g.x - qc * g.y - q3 * g.z);
  q1 += (qa * g.x + qc * g.z - q3 * g.y);
  q2 += (qa * g.y - qb * g.z + q3 * g.x);
  q3 += (qa * g.z + qb * g.y - qc * g.x);

  recipNorm = 1.0f / Xi::sqrt(q0 * q0 + q1 * q1 + q2 * q2 + q3 * q3);
  q0 *= recipNorm;
  q1 *= recipNorm;
  q2 *= recipNorm;
  q3 *= recipNorm;

  // Convert to Euler for basic rotation field
  float sinr = 2 * (q0 * q1 + q2 * q3), cosr = 1 - 2 * (q1 * q1 + q2 * q2);
  rotation.x = Xi::atan2(sinr, cosr);
  float sinp = 2 * (q0 * q2 - q3 * q1);
  rotation.y =
      (Xi::abs(sinp) >= 1) ? (sinp > 0 ? 1.57f : -1.57f) : Xi::asin(sinp);
  float siny = 2 * (q0 * q3 + q1 * q2), cosy = 1 - 2 * (q2 * q2 + q3 * q3);
  rotation.z = Xi::atan2(siny, cosy);

  deltaRotation = activeMPU->gyro;
  angularVelocity = g; // Actual fused rad/s
  north = activeMPU->mag;
}

} // namespace Xi
