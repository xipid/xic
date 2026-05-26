# Hardware Spatial ✦ Source of Truth

The `HardwareSpatial` system is the primary "Source of Truth" for all positioning, orientation, and environmental data in the Xi framework. It performs high-frequency sensor fusion across multiple hardware devices to provide a unified, filtered spatial state.

---

## Architecture

`HardwareSpatial` inherits from `TimedGeoEnviromental`, consolidating data from three distinct layers:

1.  **GeoSpatial**: Latitude, Longitude, and Altitude from GPS.
2.  **TimedTransform**: Filtered Orientation (Roll, Pitch, Yaw) and Linear Acceleration from IMU/MPU.
3.  **Environmental**: Temperature, Humidity, and Pressure from DHT/Barometers.

### The Global Location Macro
The framework provides a global `location` macro that is linked to the active spatial state. By default, `HardwareSpatial` populates this state.

```cpp
#include <Hardware/Spatial.hpp>

// Access fused orientation
float pitch = location.rotation().y;

// Access environmental data
float temp = location.temperature;

// Access raw hardware devices
auto &gyro = space.mpu[0]->gyro;
```

---

## Sensor Fusion

The system uses a **Mahony Filter** for orientation fusion, combining accelerometer and gyroscope data to calculate a stable quaternion state, which is then converted to Euler angles for the `rotation()` property.

### Update Loop
For accurate fusion, the `update()` method must be called within your main loop. This handles the delta-time calculations and sensor synchronization automatically.

```cpp
while (true) {
  space.update();
  
  // Use fused orientation
  Vector3 r = space.rotation();
  
  Xi::sleep(10); // 100Hz fusion
}
```

---

## API Reference

### Properties
- `mpu`: Array of active MPU/IMU devices.
- `gps`: Array of active GPS/BDS devices.
- `dht`: Array of active DHT/Temperature sensors.
- `down`: Normalized vector pointing towards gravity in the body frame.
- `north`: Normalized magnetic north vector (if magnetometer present).
- `deltaPos`: Linear acceleration excluding gravity.

### Methods
- `getInstance()`: Returns the singleton hardware instance.
- `update()`: Synchronizes all hardware and recalculates fusion.
- `syncTime(u64 now)`: Aligns system time with external sources (e.g., GPS PPS).

---

> [!NOTE]
> `HardwareSpatial` is designed to be fault-tolerant. If multiple MPUs are connected, it will automatically failover to a healthy sensor if the primary fails to provide noise or interrupts.
