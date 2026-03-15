#ifndef XI_FILE_HPP
#define XI_FILE_HPP

#include "Array.hpp"
#include "Device.hpp"
#include "String.hpp"

#include <cstdio>
#include <cstring>
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

namespace Xi {

class XI_EXPORT Stat {
public:
  String path; // Relative to stat(...)

  bool isFile = false;
  bool isDir = false;
  bool isSymlink = false;
  bool isHidden = false;
  bool isReadOnly = false;

  bool isExecutableByOwner = false;
  bool isWritableByOwner = false;
  bool isReadableByOwner = false;

  // Others
  bool isExecutableByGroup = false;
  bool isWritableByGroup = false;
  bool isReadableByGroup = false;

  bool isExecutableByOthers = false;
  bool isWritableByOthers = false;
  bool isReadableByOthers = false;

  bool isRegular = false;
  bool isCharacterDevice = false;
  bool isBlockDevice = false;
  bool isFIFO = false;
  bool isSocket = false;
  bool isSymbolicLink = false;

  usz size = 0;

  Array<Stat> children; // For directories
};

class XI_EXPORT FilesystemDevice : public Device {
public:
  String workdir = ""; // Prepended to any path, escapable
  String basedir = ""; // Prepended to any path, should never be escapable

  FilesystemDevice() { name = "Filesystem"; }

  virtual String read(const String &path, u64 startPos = 0,
                      u64 maxLength = 0) = 0;
  virtual void write(const String &path, const String &content,
                     i64 startPos = 0) = 0;
  virtual void append(const String &path, const String &content) = 0;
  virtual void mkdir(const String &path) = 0;
  virtual void unlink(const String &path) = 0;
  virtual Stat stat(const String &path, i32 depth = 0, i32 maxChildren = 0) = 0;

protected:
  String resolve(const String &path);
};

class LinuxFS XI_EXPORT : public FilesystemDevice {
public:
  LinuxFS() { name = "LinuxFS"; }

  String read(const String &path, u64 startPos = 0, u64 maxLength = 0) override;
  void write(const String &path, const String &content,
             i64 startPos = 0) override;
  void append(const String &path, const String &content) override;
  void mkdir(const String &path) override;
  void unlink(const String &path) override;
  Stat stat(const String &path, i32 depth = 0, i32 maxChildren = 0) override;
};

class WindowsFS XI_EXPORT : public LinuxFS {
public:
  WindowsFS() { name = "WindowsFS"; }
};

FilesystemDevice *requestFS();

} // namespace Xi

#endif // XI_FILE_HPP