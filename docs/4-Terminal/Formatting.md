# Terminal Formatting ✦ Aesthetics

The `Terminal` module provides a premium, zero-boilerplate toolkit for creating lush Command Line Interfaces. It supports 24-bit TrueColor, linear gradients, and high-level UI components.

---

## Rich Styling

### 1. ANSI & RGB Gradients
Beyond standard 16-color palettes, Xi supports full **RGB TrueColor** gradients.

```cpp
using namespace Terminal;

// Simple bold cyan
log(Bold(Cyan("Operation Success")));

// 24-bit RGB Gradient
String text = RGB("Lush Gradient Text", 255, 0, 0, 0, 0, 255); // Red to Blue
```

### 2. Composition (Links & Icons)
Modern terminals support clickable links and high-fidelity symbols.

```cpp
// Create a clickable terminal link
String l = Link("Open Repo", "https://github.com/xi/xic");

// Use professional icons
log(Icon::Sparkle + " Build Complete");
```

---

## UI Components

### 1. Data Tables
Tables are automatically calculated based on the longest content in each column.

```cpp
Array<String> data = {
  "ID", "Status", "Latency",
  "001", "Online", "4ms",
  "002", "Offline", "-"
};

log(Table(data));
```

### 2. Content Boxes
Wrap complex output in elegant boxes with optional titles.

```cpp
log(Box("The system is now running in secure mode.", "Security Alert"));
```

---

## Progress & Activity

For long-running tasks, use the `Progress` and `Spinner` classes to provide real-time feedback.

```cpp
Spinner s("Calculating hashes...");
s.start();

// ... heavy work ...

s.stop("Hashes verified.", true);
```

### The Progress Bar
The `Progress` manager can handle multiple simultaneous linear and bitmap tasks.

```cpp
Progress p;
p.message = "Deploying Cluster";
p.tasks.push({"Nodes", "4/10", "12s"});
p.update();
```

---

## Terminal System Control

- **`Clear()`**: Wipes the screen.
- **`MoveCursor(row, col)`**: Positions the cursor precisely.
- **`RawMode`**: A RAII class to enter raw terminal mode (disabling buffering and echo).

```cpp
{
  RawMode raw; // Enters raw mode
  // ... handle single-keypress input ...
} // Automatically exits raw mode
```

---

> [!IMPORTANT]
> To ensure cross-platform compatibility, check `IsTTY()` before emitting complex ANSI sequences if your output might be redirected to a file or pipe.
