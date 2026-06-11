# Filesystem and Devices

The **Resource::File** module provides a high-level, platform-agnostic abstraction for interacting with the filesystem. It treats directories and files as a unified hierarchy and handles the differences between Unix and Windows paths internally.

---

## Filesystem Devices

xic uses a "device-based" approach to filesystem access. The `FilesystemDevice` class is the primary interface, with concrete implementations for different operating systems.

### Accessing the Filesystem
Use the factory method to get the correct OS implementation:

```cpp
using namespace Resource;

FilesystemDevice &fs = FilesystemDevice::fs();
```

### Directory Scoping
You can create scoped filesystem views to prevent your application from accessing files outside of its intended sandbox.

```cpp
// Create a scoped view for logs
FilesystemDevice &logFs = fs.fs("var/logs");

// This will read 'var/logs/latest.log'
String data = logFs.read("latest.log");
```

---

## Path Manipulation

The `Xi::Path` class is an extremely versatile utility for managing both filesystem paths and networking URLs.

-   **Protocols**: Automatically parses schemes like `http://` or `file://`.
-   **Normalization**: Resolves `.` and `..` segments automatically.
-   **Query Parameters**: Parses URL query strings into a `Map<String, String>`.

```cpp
Path p("http://xi.local:8080/api/v1/status?verbose=true");

String host = p.host();      // "xi.local"
u16 port = p.port();          // 8080
String q = p.query()["verbose"]; // "true"
```

---

## Metadata and Stats

The `Stat` class provides a rich set of information about a file or directory, including permissions, size, and recursive child listings.

```cpp
Stat info = fs.stat("src/main.cpp");

if (info.isFile) {
    usz totalBytes = info.size;
}

if (info.isDir) {
    // List all direct children
    for (auto &child : info.children) {
        printf("Found: %s\n", child.path.c_str());
    }
}
```

---

## File Operations

The API stays true to the xic philosophy: zero-exception and expressive.

```cpp
// Writing
fs.write("config.yaml", "mode: auto\ndebug: true");

// Appending
fs.append("event.log", "Event triggered at " + Time::syncClock().toString());

// Reading
String config = fs.read("config.yaml");

// Deleting
fs.unlink("old_temp_file");
```

---

## Best Practices

1.  **Strict Base Dirs**: Always set a `basedir` on your `FilesystemDevice` instances for production code to prevent directory traversal attacks.
2.  **Relative to Absolute**: Use `Path` to resolve relative paths before passing them to low-level APIs.
3.  **Buffer Allocation**: When reading very large files, check the `Stat.size` first and consider reading in chunks to avoid overwhelming the memory if you are on an embedded target.
