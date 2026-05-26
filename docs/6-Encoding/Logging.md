# Structured Logging

The **Data::Log** (aliased as **Encoding::Log** in some contexts) is a thread-safe, singleton-based logging utility designed for both embedded UART consoles and native stderr streams.

---

## Log Levels

Managing the signal-to-noise ratio is critical in production systems. xic supports five severity levels:

| Level | Description |
| :--- | :--- |
| `Verbose` | Detailed diagnostic data, usually disabled in production. |
| `Info` | General situational awareness (startup, heartbeat). |
| `Warning` | Non-fatal issues that may require investigation. |
| `Error` | Logical errors that don't stop the process. |
| `Critical` | Hardware failure or security breach (usually triggers a restart). |
| `None` | Disables all output. |

---

## Usage Patterns

### Global Shortcuts
For most cases, use the global shortcut functions for brevity.

```cpp
using namespace Data;

info("Server started on port 80");
warn("Low memory detected: 12KB remaining");
error("Failed to write to sector 0xAF00");
```

### Instance Configuration
Use the singleton instance to adjust the global log level at runtime.

```cpp
Log::getInstance().setLevel(LogLevel::Warning);

info("This will not be printed");
warn("This WILL be printed");
```

---

## Hardware Abstraction

xic handles the underlying write operations based on the target platform:
-   **ESP32/Arduino**: Automatically routes messages to `Serial.print`.
-   **Native (Linux/macOS)**: Routes messages to file descriptor `2` (stderr) using high-performance `write()` calls to avoid the overhead of `std::cout`.

---

## Best Practices

1.  **Prefer Macros for Verbose**: For extremely heavy debugging logs, consider wrapping them in `#ifdef XI_DEBUG` to eliminate the string literal overhead from the final binary.
2.  **Redirecting Output**: Since xic logs to stderr, you can easily redirect logs to a file while keeping stdout clean for data output:
    ```bash
    ./myapp > results.txt 2> error.log
    ```
3.  **Thread Safety**: The singleton is designed to be thread-safe for standard logging use cases, but avoid placing huge logic blocks inside your log calls.
4.  **No Newlines Needed**: The `info()`, `warn()`, and `error()` helpers automatically append a newline character for you.
    ```cpp
    info("Done"); // Prints "Done\n"
    ```
