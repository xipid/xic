# Transform & Spatial Reasoning

The **Spatial** system in the Xi framework provides a robust foundation for representing and manipulating 3D objects, geographic locations, and physical dynamics.

---

## The Transform Hierarchy

The framework uses a progressive inheritance model to enrich spatial data with context:

### 1. Transform
The base structure representing an object's state in 3D space:
- **`position`**: `Vector3` (X, Y, Z).
- **`rotation`**: `Vector3` (Roll, Pitch, Yaw in radians).
- **`scale`**: `Vector3` (X, Y, Z).

#### Versioning & Matrix Caching
`Transform` implements a lazy-evaluation matrix system. Every time a property is modified (`setPosition`, etc.), the `transformVersion` is incremented. The 4x4 Transformation Matrix is only recalculated when requested and the version has changed.

```cpp
Transform t;
t.setPosition({10, 0, 5});
t.setRotation({0, PI/2, 0});

// Recalculates matrix once
Matrix4 mat = t.getMatrix();
```

### 2. GeoSpatial
Extends `Transform` with **WGS84 Ellipsoid** geodesy. It allows conversion between binary cartesian coordinates (X, Y, Z) and Geographic coordinates (Latitude, Longitude, Altitude).

```cpp
GeoSpatial drone;
// Set position to London (Lat/Lon/Alt)
drone.setGeoPos({51.5074f, -0.1278f, 100.0f});

// Retrieve cartesian Earth-Centered coordinates
Vector3 world = drone.getPosition();
```

### 3. TimedTransform
Adds **Dynamics** and **Calculus** to the spatial state. It tracks `dt` (Delta Time), `velocity`, and `angularVelocity`. Use the `sync(micros)` method to update the kinematics automatically based on position changes.

---

## Geodesy (WGS84)

Xi implements the standard **WGS84 Earth Model** for planetary-scale positioning:
- **Semi-major axis**: 6,378,137.0 m
- **Flattening**: 1 / 298.257223563

This allows high-precision navigation and global coordinate synchronization without floating-point drift at local scales.

---

## The location Macro
A project-wide singleton `location` (of type `TimedGeoEnviromental`) acts as the "Player" or "System" source of truth.

```cpp
#include <Xi/Transform.hpp>

// Get current system altitude
float alt = location.getGeoPosition().z;

// Check how fast we are moving
float speed = location.velocity.length();
```

---

> [!TIP]
> Use `touch()` manually if you are modifying raw position fields directly and want to invalidate the matrix cache.
