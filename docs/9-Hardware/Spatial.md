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

## Ground Station (GS) Interface

For remote sensing and telemetry, the framework provides **SoftwareGS** and **HardwareGS** interfaces. These classes collect physical sensor inputs, perform mathematical state synchronization, and can register themselves as the global location provider.

```cpp
#include <Hardware/GS.hpp>
using namespace Xi;

// Create a hardware ground station
HardwareGS gs;

// Register physical sensors
gs.mpus.push(myMpu);
gs.dht.push(myDht);
gs.setGPS(myGps);

// Claim the global location singleton so that 'location' macro accesses this GS
gs.take();

while (true) {
    gs.update(); // Polls sensors, runs Mahony fusion, updates global position
    Xi::sleep(20);
}
```

### SoftwareGS Reference

* **Properties**:
  * `bool gps`: True if GPS synchronization is active.
  * `bool motion`: True if motion/inertial tracking is active.
  * `bool enviro`: True if environmental sensing is active.
* **Methods**:
  * `bool gpsAvailable() const`: Returns true if a valid GPS signal is found.
  * `bool motionAvailable() const`: Returns true if inertial data is available.
  * `bool enviroAvailable() const`: Returns true if temp/humidity data is available.
  * `virtual void update()`: Updates internal clocks and synchronizes telemetry.
  * `void take()`: Assigns this ground station instance as the primary system location source of truth.

### HardwareGS Reference

Inherits from `SoftwareGS` and coordinates physical sensors.

* **Properties**:
  * `Collection::InlineArray<MPUDevice *> mpus`: Registered inertial measurement units.
  * `Collection::InlineArray<DHTDevice *> dht`: Registered temperature and humidity sensors.
  * `Collection::InlineArray<GPSDevice *> gpsList`: Registered GPS receivers.
* **Methods**:
  * `void setGPS(GPSDevice *g)`: Registers a GPS receiver.
  * `void update() override`: Polls all connected sensors, runs a Mahony sensor fusion filter to compute orientation quaternions, converts orientation to Euler angles, syncs time, and averages environmental inputs.

---

> [!NOTE]
> `HardwareSpatial` and `HardwareGS` are designed to be fault-tolerant. If multiple sensors of the same type are connected, they will automatically average reading values or failover to active sensors.
