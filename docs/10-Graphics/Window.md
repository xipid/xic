# Window and Screen Management

The **Graphics** module provides a high-level, vendor-neutral interface for creating OS windows and managing display screens. It abstracts the complexities of GLFW and Diligent Engine behind a simple `Device`-based architecture.

---

## Requesting a Window

Creating a native window is handled by the `requestWindow()` factory. This returns a standard xic `Device` that represents the application window.

```cpp
using namespace Graphics;

// Create a native window (GLFW + Diligent Context)
Device *win = requestWindow();

// Main poll loop
while (!win->shouldRelease) {
    win->update(); // Polls events and swaps buffers
}
```

---

## Screen Configuration

The `DeviceScreen` represents the drawable area of the window. You can retrieve it from the window device to configure titles, dimensions, and rendering hardware.

```cpp
DeviceScreen *screen = win->screen();

// Configuration
screen->title = "Xi Universe Viewer";
screen->width = 1920;
screen->height = 1080;

// The Hardware Rendering Device (Diligent)
IMemoryDevice *gpu = screen->renderingDevice;
```

---

## The Screen Surface

A critical component of the `DeviceScreen` is its `surface`. This is a pointer to the pixel data that will be presented to the display. By linking a camera's output surface to the screen's surface entry, you create a direct bridge from the 3D scene to the OS window.

```cpp
// Direct linkage
screen->surface = &camera.surface;
```

---

## Best Practices

1.  **Poll Rate**: Call `win->update()` every frame to keep the window responsive to OS events (resizing, closing, moving).
2.  **Explicit Initialization**: Always check if `win->screen()` returns a valid pointer before attempting to configure the display.
3.  **Clean Exit**: Use `delete win` at the end of your program to properly release native window handles and GPU swap chains.
