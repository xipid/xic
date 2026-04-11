#ifndef XI_HARDWARE_GS_HPP
#define XI_HARDWARE_GS_HPP

#include "../Collection/InlineArray.hpp"
#include "../Xi/Math.hpp"
#include "../Xi/Primitives.hpp"
#include "../Xi/Time.hpp"
#include "../Xi/Transform.hpp"
#include "DHT.hpp"
#include "GPS.hpp"
#include "MPU.hpp"

namespace Xi {

/**
 * @class SoftwareGS
 * @brief Software Ground Station / Global System interface.
 */
class XI_EXPORT SoftwareGS : public TimedGeoEnviromental {
public:
  bool gps = false;
  bool motion = false;
  bool enviro = false;

  bool gpsAvailable() const { return _gpsAvailable; }
  bool motionAvailable() const { return _motionAvailable; }
  bool enviroAvailable() const { return _enviroAvailable; }

  virtual void update() {
    // Default implementation does nothing or pulls from network/simulation
    sync(Xi::epochMicros());
  }

  /** @brief Claims the global location singleton. */
  void take() { Xi::Internal::locationPtr() = this; }

protected:
  bool _gpsAvailable = false;
  bool _motionAvailable = false;
  bool _enviroAvailable = false;
};

/**
 * @class HardwareGS
 * @brief Hardware implementation of the Ground Station, managing physical
 * sensors.
 */
class XI_EXPORT HardwareGS : public SoftwareGS {
public:
  Collection::InlineArray<MPUDevice *> mpus;
  Collection::InlineArray<DHTDevice *> dht;
  Collection::InlineArray<GPSDevice *> gpsList;

  HardwareGS() {
    // Initial quaternion (W=1, XYZ=0)
    q[0] = 1.0f;
    q[1] = 0.0f;
    q[2] = 0.0f;
    q[3] = 0.0f;
  }

  void setGPS(GPSDevice *g) { gpsList.push(g); }

  void update() override {
    u64 now = Xi::epochMicros();
    sync(now);
    if (dt <= 0)
      return;

    // 1. Poll Sensors
    Vector3 sumAcc = {0, 0, 0}, sumGyro = {0, 0, 0}, sumMag = {0, 0, 0};
    int mpuCount = 0;
    for (usz i = 0; i < mpus.size(); ++i) {
      auto *m = mpus[i];
      m->update();
      sumAcc = sumAcc + m->accel;
      sumGyro = sumGyro + m->gyro;
      sumMag = sumMag + m->mag;
      mpuCount++;
    }

    if (mpuCount > 0) {
      _motionAvailable = true;
      Vector3 avgAcc = sumAcc * (1.0f / mpuCount);
      Vector3 avgGyro = sumGyro * (1.0f / mpuCount);
      Vector3 avgMag = sumMag * (1.0f / mpuCount);

      // 2. Mahony Fusion
      applyMahony(avgAcc, avgGyro, avgMag, dt);

      // 3. Update Rotation (WXYZ to Euler)
      _rotation = quatToEuler(q);
    }

    // 4. GPS & Time Sync
    int gpsFixes = 0;
    Vector3 avgLla = {0, 0, 0};
    for (usz i = 0; i < gpsList.size(); ++i) {
      auto *g = gpsList[i];
      g->update();
      if (g->hasFix) {
        avgLla = avgLla + g->pos;
        gpsFixes++;
        // Sync system time if needed
        if (g->lastTimeSync > now - 1000000) {
          // Update internal epoch if GPS time is fresh
          lastMicros = g->lastTimeSync;
        }
      }
    }

    if (gpsFixes > 0) {
      _gpsAvailable = true;
      this->setGeoPos(avgLla * (1.0f / (f32)gpsFixes));
    }

    // 5. Environmental
    f32 sumT = 0, sumP = 0, sumH = 0;
    int dhtCount = 0;
    for (usz i = 0; i < dht.size(); ++i) {
      auto *d = dht[i];
      d->update();
      sumT += d->temperature;
      sumH += d->humidity;
      // Pressure usually from BMP/BME, let's assume it's here or in MPU
      dhtCount++;
    }
    if (dhtCount > 0) {
      _enviroAvailable = true;
      temperature = sumT / dhtCount;
      humidity = sumH / dhtCount;
    }
  }

private:
  float q[4];                      // Quaternion [w, x, y, z]
  float integralFB[3] = {0, 0, 0}; // Integral feedback for Mahony

  // Mahony Gains
  static constexpr float Kp = 2.0f * 5.0f;
  static constexpr float Ki = 0.0f;

  void applyMahony(Vector3 a, Vector3 g, Vector3 m, float dt) {
    float q0 = q[0], q1 = q[1], q2 = q[2], q3 = q[3];
    float recNorm;
    float ax = a.x, ay = a.y, az = a.z;
    float gx = g.x, gy = g.y, gz = g.z;
    float mx = m.x, my = m.y, mz = m.z;

    // Normalize accelerometer measurement
    recNorm = 1.0f / sqrt(ax * ax + ay * ay + az * az);
    ax *= recNorm;
    ay *= recNorm;
    az *= recNorm;

    // Estimated direction of gravity
    float vx = 2.0f * (q1 * q3 - q0 * q2);
    float vy = 2.0f * (q0 * q1 + q2 * q3);
    float vz = q0 * q0 - q1 * q1 - q2 * q2 + q3 * q3;

    // Error is cross product between estimated direction and measured direction
    // of gravity
    float ex = (ay * vz - az * vy);
    float ey = (az * vx - ax * vz);
    float ez = (ax * vy - ay * vx);

    if (Ki > 0.0f) {
      integralFB[0] += ex * Ki * dt;
      integralFB[1] += ey * Ki * dt;
      integralFB[2] += ez * Ki * dt;
      gx += integralFB[0];
      gy += integralFB[1];
      gz += integralFB[2];
    }

    // Apply feedback terms
    gx += Kp * ex;
    gy += Kp * ey;
    gz += Kp * ez;

    // Integrate rate of change of quaternion
    float pa = q1, pb = q2, pc = q3;
    q0 += (-q1 * gx - q2 * gy - q3 * gz) * (0.5f * dt);
    q1 += (q0 * gx + pb * gz - pc * gy) * (0.5f * dt);
    q2 += (q0 * gy - pa * gz + pc * gx) * (0.5f * dt);
    q3 += (q0 * gz + pa * gy - pb * gx) * (0.5f * dt);

    // Normalize quaternion
    recNorm = 1.0f / sqrt(q0 * q0 + q1 * q1 + q2 * q2 + q3 * q3);
    q[0] = q0 * recNorm;
    q[1] = q1 * recNorm;
    q[2] = q2 * recNorm;
    q[3] = q3 * recNorm;
  }

  Vector3 quatToEuler(float *q) {
    float q0 = q[0], q1 = q[1], q2 = q[2], q3 = q[3];
    Vector3 euler;
    // Roll (x-axis rotation)
    float sinr_cosp = 2.0f * (q0 * q1 + q2 * q3);
    float cosr_cosp = 1.0f - 2.0f * (q1 * q1 + q2 * q2);
    euler.x = atan2(sinr_cosp, cosr_cosp) * (180.0f / PI);

    // Pitch (y-axis rotation)
    float sinp = 2.0f * (q0 * q2 - q3 * q1);
    if (abs(sinp) >= 1)
      euler.y = sgn(sinp) * (PI / 2.0f) * (180.0f / PI);
    else
      euler.y = asin(sinp) * (180.0f / PI);

    // Yaw (z-axis rotation)
    float siny_cosp = 2.0f * (q0 * q3 + q1 * q2);
    float cosy_cosp = 1.0f - 2.0f * (q2 * q2 + q3 * q3);
    euler.z = atan2(siny_cosp, cosy_cosp) * (180.0f / PI);

    return euler;
  }
};

} // namespace Xi

#endif
