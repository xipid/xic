# Window and Screen Management

The **Graphics** module provides a high-level, vendor-neutral interface for creating OS windows and managing display screens. It abstracts the complexities of GLFW and Diligent Engine behind a simple architecture.

---

## Requesting a Window

Creating a native window is handled by the `requestWindow()` factory. This returns a pointer to a `GLFWDiligentWindow` that represents the application window.

```cpp
using namespace Graphics;

// Create a native window (GLFW + Diligent Context)
GLFWDiligentWindow *win = requestWindow();

// Main poll loop
while (!win->shouldRelease) {
    win->update(); // Polls events and swaps buffers
}
```

---

## Screen Configuration

The `Screen` class represents the drawable area of the window. You can retrieve it from the window device to configure titles, dimensions, and query the rendering hardware interface.

```cpp
Screen *screen = win->screen();

// Configuration
screen->title = "Xi Universe Viewer";
screen->width = 1920;
screen->height = 1080;

// The Hardware Rendering Device (Diligent)
Xi::MemoryDevice *gpu = screen->gpu;
```

---

## The Screen Surface

A critical component of the `Screen` is its `surface`. This is a pointer to a string (`Collection::String*`) containing the pixel data that will be presented to the display. By linking a camera's output surface to the screen's surface entry, you create a direct bridge from the 3D scene to the OS window.

```cpp
// Direct linkage
screen->surface = &camera.surface;
```

---

## Best Practices

1.  **Poll Rate**: Call `win->update()` every frame to keep the window responsive to OS events (resizing, closing, moving).
2.  **Explicit Initialization**: Always check if `win->screen()` returns a valid pointer before attempting to configure the display.
3.  **Clean Exit**: Use `delete win` at the end of your program to properly release native window handles and GPU swap chains.
