# Industrial Grade Sensors

The **Hardware** module provides standardized, non-blocking interfaces for interacting with physical sensors and peripherals. It abstracts the underlying I2C/SPI/Serial protocols into a property-based API.

---

## Device Lifecycle

All hardware in Xi inherits from the `Device` base class. This ensures a consistent pattern for initialization, updates, and power management.

### The Unified Interface
- **`update()`**: Fetches raw data and performs internal processing.
- **`calibrate()`**: Performs zero-point adjustments (if applicable).
- **`isReady()`**: Returns true if the device is responsive on the bus.

---

## Motion Tracking (IMU)

The `MPUDevice` (e.g., MPU9250) provides precise 9-axis data.

```cpp
#include <Hardware/MPU.hpp>

MPU9250 mpu(0x68); // I2C Address

void setup() {
  mpu.calibrate(); // Calibrate at rest
}

void loop() {
  mpu.update();
  Vector3 gravity = mpu.accel; // G-force
  Vector3 rotRate = mpu.gyro;  // deg/s
}
```

---

## Global Positioning (GPS)

The `GPSDevice` handles high-level NMEA parsing and time synchronization.

```cpp
#include <Hardware/GPS.hpp>

UbloxGPS gps(TX_PIN, RX_PIN);

void loop() {
  gps.update();
  
  if (gps.hasFix) {
    // Lat, Lon, Alt in Vector3
    Vector3 lla = gps.pos; 
    u64 time = gps.lastTimeSync;
  }
}
```

---

## Environmental Sensing (DHT)

High-performance drivers for temperature and humidity sensors.

```cpp
#include <Hardware/DHT.hpp>

DHT22 dht(PIN_4);

void loop() {
  dht.update();
  float temp = dht.temperature;
  float hum  = dht.humidity;
}
```

---

## Best Practices

### 1. Sampling Rates
Different sensors have physical refresh limits. For instance, a DHT22 should only be polled every 2 seconds, while an MPU9250 can handle 1kHz+.

### 2. The Source of Truth
Individual sensor data is automatically aggregated into the `HardwareSpatial` system (the `space` instance). For most applications, you should query `space` or the global `location` object rather than individual devices.

```cpp
// This combines GPS, IMU, and DHT data automatically
float currentHumidity = location.humidity;
```

---

> [!CAUTION]
> Always ensure your I2C pull-up resistors are correctly sized for your bus speed (e.g. 2.2kΩ for 400kHz Fast Mode) to prevent sensor timing errors.
