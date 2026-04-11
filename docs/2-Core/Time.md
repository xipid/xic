# Time and Chronometry

The **Xi::Time** utility is a high-precision timestamp and calendar manager. It stores time as microseconds since the Unix epoch and provides a fluent API for date manipulation.

---

## Core Concepts

### Microsecond Precision
Unlike standard libraries that might stop at milliseconds, xic focuses on microseconds (`i64`). This is essential for low-latency networking and precise sensor sampling on embedded hardware.

### Property Syntax
xic introduces a Python-inspired property syntax for intuitive date manipulation:

```cpp
Time t = Time::syncClock();
t.year = 2024;
t.month += 1; // Increment month
t.day = 15;

printf("Year is %d\n", (int)t.year);
```

---

## Static Utilities

### Global Clock Sync
You'll typically synchronize the global clock once at startup.

```cpp
// Sync with system time
Time::syncClock();

// Or manual sync
Time::syncClock(1712847600000000ULL);
```

### Sleep
High-precision sleep using doubles for fractional seconds.

```cpp
Time::sleep(0.5); // 500ms
Time::sleep(0.001); // 1ms
```

---

## Formatting

The `toString` method uses a simple format string to generate readable dates.

```cpp
Time now;
now.us = epochMicros();

// Standard format
String s1 = now.toString("yyyy/mm/dd hh:mm:ss");

// Custom format
String s2 = now.toString("hh:mm:ss");
```

---

## Reference

| Method | Description |
| :--- | :--- |
| `epochMicros()` | Static: returns raw micros since epoch. |
| `isLeap(int y)` | Static: leap year check. |
| `daysInMonth(m, y)` | Static: returns days count. |
| `tz` | Timezone offset in seconds. |
| `us` | Raw microsecond value. |
| `year`, `month`, `day` | Properties for easy access. |
| `hour`, `minute`, `second` | Properties for easy access. |
