/**
 * @file File.cpp
 * @brief Filesystem device implementation for the Xi framework (Posix/Linux).

 */

#include "../../include/Resource/File.hpp"
#include "../../include/Resource/Socket.hpp"

#ifndef _WIN32
#include <cstdio>
#include <dirent.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#endif

using namespace Xi;

namespace Resource {

String Filesystem::resolve(const String &path) {
  String fullPath = basedir;
  if (!fullPath.isEmpty() && !fullPath.endsWith("/"))
    fullPath += "/";

  String w = workdir;
  if (!w.isEmpty()) {
    fullPath += w;
    if (!fullPath.endsWith("/"))
      fullPath += "/";
  }

  fullPath += path;
  return fullPath;
}

String LinuxFS::read(const String &path, u64 startPos, u64 maxLength) {
  String p = resolve(path);
#if defined(_WIN32)
  return "";
#else
  int fd = ::open(p.c_str(), O_RDONLY);
  if (fd < 0)
    return "";

  struct stat st;
  if (fstat(fd, &st) < 0) {
    ::close(fd);
    return "";
  }
  u64 fileSize = (u64)st.st_size;

  if (startPos >= fileSize) {
    ::close(fd);
    return "";
  }

  u64 readLen = (maxLength == 0) ? (fileSize - startPos) : maxLength;
  if (startPos + readLen > fileSize)
    readLen = fileSize - startPos;

  lseek(fd, (off_t)startPos, SEEK_SET);

  String res;
  res.allocate((usz)readLen);
  ssize_t actual = ::read(fd, res.data(), (size_t)readLen);
  ::close(fd);

  if (actual < 0)
    return "";
  return res;
#endif
}

void LinuxFS::write(const String &path, const String &content, i64 startPos) {
  String p = resolve(path);
#if !defined(_WIN32)
  int flags = O_WRONLY | O_CREAT;
  if (startPos == -1) {
    flags |= O_APPEND;
  } else if (startPos == 0) {
    flags |= O_TRUNC;
  }

  int fd = ::open(p.c_str(), flags, 0644);
  if (fd >= 0) {
    if (startPos > 0) {
      lseek(fd, (off_t)startPos, SEEK_SET);
    }
    ::write(fd, content.data(), content.size());
    ::close(fd);
  }
#endif
}

void LinuxFS::append(const String &path, const String &content) {
  write(path, content, -1);
}

void LinuxFS::mkdir(const String &path) {
  String p = resolve(path);
  auto parts = p.split("/");
  String current = "";
  if (p.startsWith("/"))
    current = "/";
  for (usz i = 0; i < parts.size(); ++i) {
    if (parts[i].length() == 0)
      continue;
    current += parts[i] + "/";
#if !defined(_WIN32)
    ::mkdir(current.c_str(), 0755);
#endif
  }
}

void LinuxFS::unlink(const String &path) {
  String p = resolve(path);
  struct stat st;
#if !defined(_WIN32)
  if (lstat(p.c_str(), &st) == 0 && S_ISDIR(st.st_mode)) {
    DIR *d = opendir(p.c_str());
    if (d) {
      struct dirent *de;
      while ((de = readdir(d))) {
        if (de->d_name[0] == '.') {
          if (de->d_name[1] == '\0')
            continue;
          if (de->d_name[1] == '.' && de->d_name[2] == '\0')
            continue;
        }
        String sub = path + "/" + de->d_name;
        unlink(sub);
      }
      closedir(d);
    }
    rmdir(p.c_str());
  } else {
    ::unlink(p.c_str());
  }
#endif
}

Stat LinuxFS::stat(const String &path, i32 depth, i32 maxChildren) {
  Stat s;
  s.path = path;
  String p = resolve(path);
  struct stat st;
#if !defined(_WIN32)
  if (lstat(p.c_str(), &st) == 0) {
    s.size = (usz)st.st_size;
    s.isFile = S_ISREG(st.st_mode);
    s.isDir = S_ISDIR(st.st_mode);
    s.isSymlink = S_ISLNK(st.st_mode);
    s.isHidden = (path.startsWith(".") || path.includes("/."));
    s.isReadOnly = (access(p.c_str(), W_OK) != 0);

    s.isExecutableByOwner = (st.st_mode & S_IXUSR);
    s.isWritableByOwner = (st.st_mode & S_IWUSR);
    s.isReadableByOwner = (st.st_mode & S_IRUSR);

    s.isExecutableByGroup = (st.st_mode & S_IXGRP);
    s.isWritableByGroup = (st.st_mode & S_IWGRP);
    s.isReadableByGroup = (st.st_mode & S_IRGRP);

    s.isExecutableByOthers = (st.st_mode & S_IXOTH);
    s.isWritableByOthers = (st.st_mode & S_IWOTH);
    s.isReadableByOthers = (st.st_mode & S_IROTH);

    s.isRegular = S_ISREG(st.st_mode);
    s.isCharacterDevice = S_ISCHR(st.st_mode);
    s.isBlockDevice = S_ISBLK(st.st_mode);
    s.isFIFO = S_ISFIFO(st.st_mode);
    s.isSocket = S_ISSOCK(st.st_mode);
    s.isSymbolicLink = S_ISLNK(st.st_mode);

    if (s.isDir && depth > 0) {
      DIR *d = opendir(p.c_str());
      if (d) {
        struct dirent *de;
        int count = 0;
        while ((de = readdir(d)) && (maxChildren == 0 || count < maxChildren)) {
          if (de->d_name[0] == '.') {
            if (de->d_name[1] == '\0')
              continue;
            if (de->d_name[1] == '.' && de->d_name[2] == '\0')
              continue;
          }
          s.children.push(
              stat(path + "/" + de->d_name, depth - 1, maxChildren));
          count++;
        }
        closedir(d);
      }
    }
  }
#endif
  return s;
}

SockBind *LinuxFS::socket(const String &path) { return nullptr; }
SockStation *LinuxFS::station(const String &path) { return nullptr; }

Filesystem *Filesystem::_singleton = nullptr;
Filesystem &Filesystem::fs() {
  if (!_singleton) {
#ifdef _WIN32
    _singleton = nullptr;
#else
    _singleton = new LinuxFS();
#endif
  }
  return *_singleton;
}

Filesystem &Filesystem::fs(const String &path) {
  LinuxFS *sub = new LinuxFS(*(LinuxFS *)this);
  sub->basedir = resolve(path);
  return *sub;
}

} // namespace Resource
