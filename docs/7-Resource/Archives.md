# Virtual Filesystems and Archives

The **Resource::Archive** module provides an in-memory virtual filesystem (VFS) that can be swapped in anywhere a standard filesystem is expected. This is particularly useful for resource management in games, firmware updates, or self-contained "portable" applications.

---

## The Archive (VFS)

An `Archive` is a `FilesystemDevice` that exists entirely in RAM. It supports the full suite of file operations (`read`, `write`, `stat`, `unlink`) but provides several unique features for memory management.

### LRU Caching
To prevent exhausting system memory, the Archive uses a **Least Recently Used (LRU)** eviction policy. You can set a `maxCache` limit, and xic will automatically drop the contents of the oldest accessed files to stay within your budget.

```cpp
using namespace Resource;

Archive vfs;
vfs.maxCache = 10 * 1024 * 1024; // 10MB limit

vfs.write("assets/texture.png", largeImageData);
```

### Metadata Persistence
Even when a file is evicted from cache, its metadata (size, name, permissions) remains in the `stat` tree. The `cached` flag in the `VFSEntry` will simply toggle to false until the file is accessed again.

---

## ZIP Archives

The `ZIPArchive` class extends the standard Archive with support for the industry-standard ZIP format. It allows you to mount a compressed ZIP file as a virtual directory.

### Lazy Loading
One of xic's most advanced features is **lazy block-level decompression**. You don't need to decompress the entire ZIP into RAM to read a single file. You can even provide a callback to fetch ZIP blocks from an external source (like an HTTP range request or a flash memory offset).

```cpp
ZIPArchive zip;

// Resolve blocks lazily from a hardware offset
zip.onFormatRequest([](u64 pos, u64 len) {
    return Hardware::Flash::read(BLOCK_START + pos, len);
});

// The file 'main.lua' is only decompressed into memory when this line runs
String script = zip.read("main.lua");
```

### Creating ZIPs
You can also use the `ZIPArchive` to bundle your virtual filesystem back into a compressed blob.

```cpp
String bundle = zip.formatCompressed();
fs.write("backup.zip", bundle);
```

---

## Use Cases

1.  **Game Assets**: Pack all your textures and sounds into a single `.pak` (ZIP) file and mount it at startup.
2.  **Firmware Updates**: Download an update package into a `ZIPArchive` to verify its contents before applying them to the physical flash.
3.  **Unit Testing**: Use a vanilla `Archive` as a "mock" filesystem in your tests to avoid writing to the actual disk.

---

## Best Practices

1.  **Cache Tuning**: For ESP32 targets, set `maxCache` to a very small value (e.g., 64KB) to ensure you don't trigger a heap overflow.
2.  **Explicit Flush**: If you've modified many files and want to ensure they stay in RAM, you might need to temporarily increase `maxCache`.
3.  **Conditionals**: `ZIPArchive` requires `miniz`. Ensure `XI_DEFLATE_ENABLED` is defined in your build configuration.
