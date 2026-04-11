/**
 * @file Archive.hpp
 * @brief In-memory virtual filesystem and ZIP archive for the Xi framework.
 *
 * Archive: A full virtual filesystem backed by RAM with LRU cache eviction.
 *          Extends FilesystemDevice so it can be used anywhere a filesystem is
 *          expected (read, write, stat, mkdir, unlink, append).
 *
 * ZIPArchive: Extends Archive with ZIP format parsing/writing via miniz.
 *             Compiled only when XI_DEFLATE_ENABLED is defined.
 */

#ifndef XI_RESOURCE_ARCHIVE_HPP
#define XI_RESOURCE_ARCHIVE_HPP

#include "File.hpp"
#include "../Collection/Map.hpp"
#include "../Xi/Func.hpp"

namespace Resource {

using namespace Xi;
using namespace Collection;

/**
 * @struct VFSEntry
 * @brief A single node in the virtual filesystem tree.
 */
struct XI_EXPORT VFSEntry {
  String content;          ///< File data (empty for directories).
  bool isDir = false;      ///< True if directory.
  u64 lastAccess = 0;      ///< Timestamp for LRU eviction.
  bool cached = false;     ///< True if content is resident in RAM.
};

/**
 * @class Archive
 * @brief Full in-memory virtual filesystem with LRU caching.
 *
 * All FilesystemDevice methods are implemented against an in-memory tree.
 * When `maxCache` is exceeded, the least recently accessed files are evicted
 * (content cleared, `cached` set to false). Directories are never evicted.
 *
 * A `maxCache` of 0 means no content is kept — metadata (stat) still works.
 */
class XI_EXPORT Archive : public FilesystemDevice {
public:
  usz maxCache = 1024 * 1024; ///< Max bytes kept in RAM (default 1 MB).

  Archive();
  virtual ~Archive();

  // --- FilesystemDevice interface ---
  String read(const String &path, u64 startPos = 0,
              u64 maxLength = 0) override;
  void write(const String &path, const String &content,
             i64 startPos = 0) override;
  void append(const String &path, const String &content) override;
  void mkdir(const String &path) override;
  void unlink(const String &path) override;
  Stat stat(const String &path, i32 depth = 0, i32 maxChildren = 0) override;
  SockBind *socket(const String &path = "") override;
  SockStation *station(const String &path = "") override;

  /**
   * @brief Returns the total bytes of file content currently in cache.
   */
  usz cacheUsed() const { return _cacheUsed; }

  /**
   * @brief Checks if a file path exists.
   */
  bool exists(const String &path) const;

  /**
   * @brief Lists immediate children of a directory.
   */
  Array<String> list(const String &path) const;

  /**
   * @brief Evicts least-recently-used file content until cacheUsed <= maxCache.
   */
  void evict();

protected:
  Map<String, VFSEntry> _entries; ///< Path → entry map.
  usz _cacheUsed = 0;            ///< Current bytes in cache.

  String _normalize(const String &path) const;
  void _ensureParents(const String &path);
  void _touch(VFSEntry &e);
};

// =========================================================================
// ZIPArchive (miniz)
// =========================================================================

#ifdef XI_DEFLATE_ENABLED

/**
 * @class ZIPArchive
 * @brief ZIP format virtual filesystem extending Archive.
 *
 * Can load an existing ZIP file into the virtual FS, or serialize the
 * current virtual FS contents back into ZIP format.
 *
 * The raw ZIP data can reside externally — the `onFormatRequest` callback
 * provides lazy block-level access so the entire ZIP need not be in RAM.
 *
 * Usage:
 *   ZIPArchive zip;
 *   zip.load(zipBlob);            // Parse existing ZIP
 *   String data = zip.read("file.txt");
 *   zip.write("new.txt", "hello");
 *   String out = zip.formatCompressed();  // Serialize back to ZIP
 */
class XI_EXPORT ZIPArchive : public Archive {
public:
  ZIPArchive();
  ~ZIPArchive() override;

  /**
   * @brief Registers a callback for lazy loading of raw ZIP data.
   *
   * Called on cache miss when a file is accessed but not yet decompressed.
   * The callback receives (position, length) into the raw ZIP blob and
   * must return the corresponding bytes.
   */
  void onFormatRequest(Func<String(u64, u64)> cb);

  /**
   * @brief Loads and parses an existing ZIP blob into the virtual FS.
   * @param zipData The complete ZIP file in memory.
   * @return true on success.
   */
  bool load(const String &zipData);

  /**
   * @brief Total size of the underlying raw ZIP data.
   */
  usz formatSize() const;

  /**
   * @brief Serializes the entire virtual FS into a new ZIP blob.
   * @return The compressed ZIP data.
   */
  String formatCompressed();

  /**
   * @brief Reads raw bytes from the underlying ZIP data at a given offset.
   * @param position Byte offset into the ZIP.
   * @param length Number of bytes to read.
   * @return Raw (not decompressed) ZIP bytes.
   */
  String formatRead(u64 position, u64 length);

  // Override read to support lazy decompression from ZIP
  String read(const String &path, u64 startPos = 0,
              u64 maxLength = 0) override;

private:
  Func<String(u64, u64)> _formatRequestCb;
  String _rawZip;           ///< The raw ZIP blob (if loaded in memory).

  /// Per-file ZIP metadata for lazy decompression.
  struct ZIPFileInfo {
    u64 localHeaderOffset;  ///< Offset of local file header in ZIP.
    u64 compressedSize;     ///< Compressed size in ZIP.
    u64 uncompressedSize;   ///< Original file size.
    u16 compressionMethod;  ///< 0 = stored, 8 = deflate.
  };
  Map<String, ZIPFileInfo> _zipIndex; ///< Path → ZIP metadata.

  bool _parseDirectory();
  String _decompressEntry(const ZIPFileInfo &info);
};

#endif // XI_DEFLATE_ENABLED

} // namespace Resource

#endif // XI_RESOURCE_ARCHIVE_HPP
