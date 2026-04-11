# Getting Started

Getting up and running with **xic** is straightforward, whether you're targeting an ESP32 or a native Linux environment.

---

## Installation

### PlatformIO (ESP32)
The most common way to use xic is within the PlatformIO ecosystem. Add the following to your `platformio.ini`:

```ini
[env:esp32]
platform = espressif32
framework = arduino
lib_deps =
    xic
build_flags =
    -O3
    -Iinclude
```

### Manual Integration
If you're integrating into a custom build system, ensure you include the `include` directory and link the Monocypher sources found in `packages/monocypher`.

---

## Your First Program

Here is a minimal example that initializes the xic environment and logs a message to the terminal.

```cpp
#include <Xi/Time.hpp>
#include <Terminal/Format.hpp>

using namespace Xi;
using namespace Terminal;

int main() {
    // Synchronize the global clock
    Time::syncClock();

    // Use rich terminal formatting
    Success(Bold("xic environment initialized."));

    String currentTime = Time().toString("hh:mm:ss");
    Info("The current time is: " + Blue(currentTime));

    return 0;
}
```

---

## Project Structure

A typical xic project follows this layout:

-   `include/`: Your header files.
-   `src/`: Your implementation files.
-   `packages/`: Third-party dependencies (like Monocypher).
-   `docs/`: Where this documentation lives.

---

## Best Practices

1.  **Always Prefer `String`**: Use `Collection::String` instead of `std::string`. It is optimized for small buffer optimizations and zero-exception handling.
2.  **Sync the Clock**: Many modules depend on accurate timing. Call `Time::syncClock()` early in your execution flow.
3.  **Check TTY**: If building for CLI tools, use `Terminal::IsTTY()` to decide whether to emit ANSI color codes.
