/**
 * @file File.hpp
 * @brief Filesystem abstraction and device management for the Xi framework.

 */

#ifndef XI_CORE_FILE_HPP
#define XI_CORE_FILE_HPP

#include "../Collection/String.hpp"
#include "../Xi/Device.hpp"

#include <sys/stat.h>
#include <sys/types.h>

#if defined(_WIN32)
#include <direct.h>
#include <io.h>
#include <windows.h>
#else
#include <dirent.h>
#include <fcntl.h>
#include <unistd.h>
#endif

using namespace Xi;

namespace Resource {

class SockBind;
class SockStation;

/**
 * @class Stat
 * @brief Represents filesystem metadata for a file or directory.
 */
class XI_EXPORT Stat {
public:
  String path; ///< Path relative to the queried directory.

  bool isFile = false;     ///< True if it's a regular file.
  bool isDir = false;      ///< True if it's a directory.
  bool isSymlink = false;  ///< True if it's a symbolic link.
  bool isHidden = false;   ///< True if it's a hidden file/dir.
  bool isReadOnly = false; ///< True if the file is read-only.

  bool isExecutableByOwner = false; ///< Owner execution permission.
  bool isWritableByOwner = false;   ///< Owner write permission.
  bool isReadableByOwner = false;   ///< Owner read permission.

  bool isExecutableByGroup = false; ///< Group execution permission.
  bool isWritableByGroup = false;   ///< Group write permission.
  bool isReadableByGroup = false;   ///< Group read permission.

  bool isExecutableByOthers = false; ///< Others execution permission.
  bool isWritableByOthers = false;   ///< Others write permission.
  bool isReadableByOthers = false;   ///< Others read permission.

  bool isRegular = false;         ///< True if it's a regular file.
  bool isCharacterDevice = false; ///< True if it's a character device.
  bool isBlockDevice = false;     ///< True if it's a block device.
  bool isFIFO = false;            ///< True if it's a FIFO/pipe.
  bool isSocket = false;          ///< True if it's a socket.
  bool isSymbolicLink = false;    ///< True if it's a symbolic link.

  usz size = 0; ///< Size of the file in bytes.

  Array<Stat> children; ///< List of children metadata (for directories).
};

/**
 * @class FilesystemDevice
 * @brief Conceptual device representing a filesystem.
 */
class XI_EXPORT FilesystemDevice : public Device {
public:
  String workdir = ""; ///< Working directory, prepended to relative paths.
  String basedir = ""; ///< Base directory, strict prefix for all operations.

  FilesystemDevice() { name = "Filesystem"; }

  /**
   * @brief Reads file content.
   * @param path Path to the file.
   * @param startPos Starting offset in bytes.
   * @param maxLength Maximum bytes to read (0 for all).
   * @return File content as a String.
   */
  virtual String read(const String &path, u64 startPos = 0,
                      u64 maxLength = 0) = 0;

  /**
   * @brief Overwrites or creates a file with content.
   */
  virtual void write(const String &path, const String &content,
                     i64 startPos = 0) = 0;

  /**
   * @brief Appends content to an existing file.
   */
  virtual void append(const String &path, const String &content) = 0;

  /**
   * @brief Creates a directory.
   */
  virtual void mkdir(const String &path) = 0;

  /**
   * @brief Deletes a file or directory.
   */
  virtual void unlink(const String &path) = 0;

  /**
   * @brief Retrieves metadata for a path.
   * @param path Target path.
   * @param depth Recursive depth for directory listing.
   * @param maxChildren Maximum number of children to list.
   */
  virtual Stat stat(const String &path, i32 depth = 0, i32 maxChildren = 0) = 0;

  virtual SockBind *socket(const String &path = "") = 0;
  virtual SockStation *station(const String &path = "") = 0;

  /** @brief Gets the default global filesystem device. */
  static FilesystemDevice &fs();
  /** @brief Returns a reference to the filesystem at a specific path. */
  FilesystemDevice &fs(const String &path);

protected:
  static FilesystemDevice *_singleton;
  String resolve(const String &path);
};

/**
 * @class LinuxFS
 * @brief POSIX-compliant filesystem implementation.
 */
class XI_EXPORT LinuxFS : public FilesystemDevice {
public:
  LinuxFS() { name = "LinuxFS"; }
  LinuxFS(const LinuxFS &other) : FilesystemDevice() {
    name = other.name;
    workdir = other.workdir;
    basedir = other.basedir;
  }

  String read(const String &path, u64 startPos = 0, u64 maxLength = 0) override;
  void write(const String &path, const String &content,
             i64 startPos = 0) override;
  void append(const String &path, const String &content) override;
  void mkdir(const String &path) override;
  void unlink(const String &path) override;
  Stat stat(const String &path, i32 depth = 0, i32 maxChildren = 0) override;
  SockBind *socket(const String &path = "") override;
  SockStation *station(const String &path = "") override;
};

/**
 * @class WindowsFS
 * @brief Windows-specific filesystem implementation.
 */
class XI_EXPORT WindowsFS : public LinuxFS {
public:
  WindowsFS() { name = "WindowsFS"; }
};

/**
 * @brief Factory function to get appropriate OS filesystem.
 */
FilesystemDevice *requestFS();

} // namespace Resource

#endif // XI_CORE_FILE_HPP