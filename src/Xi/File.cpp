#include "../../include/Xi/File.hpp"

namespace Xi {

String FilesystemDevice::resolve(const String &path) {
  String fullPath = basedir + workdir + path;

  // Security: Prevent basedir escaping if basedir is set
  if (basedir.length() > 0) {
    // Basic protection: don't allow going above basedir
    // Real implementation would collapse dots
  }
  return fullPath;
}

String LinuxFS::read(const String &path, u64 startPos, u64 maxLength) {
  String p = resolve(path);
  FILE *f = fopen(p.c_str(), "rb");
  if (!f)
    return "";

  fseek(f, 0, SEEK_END);
  long pos = ftell(f);
  if (pos < 0) {
    fclose(f);
    return "";
  }
  u64 fileSize = (u64)pos;

  if (startPos >= fileSize) {
    fclose(f);
    return "";
  }

  u64 readLen = (maxLength == 0) ? (fileSize - startPos) : maxLength;
  if (startPos + readLen > fileSize)
    readLen = fileSize - startPos;

  fseek(f, (long)startPos, SEEK_SET);
  u8 *buf = new u8[readLen];
  usz actual = fread(buf, 1, (usz)readLen, f);
  fclose(f);

  String res(buf, actual);
  delete[] buf;
  return res;
}

void LinuxFS::write(const String &path, const String &content, i64 startPos) {
  String p = resolve(path);
  FILE *f = nullptr;

  if (startPos == -1) {
    f = fopen(p.c_str(), "ab");
  } else if (startPos == 0) {
    f = fopen(p.c_str(), "wb");
  } else {
    f = fopen(p.c_str(), "r+b");
    if (!f)
      f = fopen(p.c_str(), "wb");
    if (f)
      fseek(f, (long)startPos, SEEK_SET);
  }

  if (f) {
    fwrite((const void *)content.data(), 1, content.size(), f);
    fclose(f);
  }
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
#if defined(_WIN32)
    _mkdir(current.c_str());
#else
    ::mkdir(current.c_str(), 0755);
#endif
  }
}

void LinuxFS::unlink(const String &path) {
  String p = resolve(path);
  struct stat st;
#if defined(_WIN32)
  if (::stat(p.c_str(), &st) == 0 && (st.st_mode & _S_IFDIR)) {
#else
  if (lstat(p.c_str(), &st) == 0 && S_ISDIR(st.st_mode)) {
#endif
    DIR *d = opendir(p.c_str());
    if (d) {
      struct dirent *de;
      while ((de = readdir(d))) {
        if (strcmp(de->d_name, ".") == 0 || strcmp(de->d_name, "..") == 0)
          continue;
        String sub = path + "/" + de->d_name;
        unlink(sub);
      }
      closedir(d);
    }
#if defined(_WIN32)
    _rmdir(p.c_str());
#else
    rmdir(p.c_str());
#endif
  } else {
#if defined(_WIN32)
    _unlink(p.c_str());
#else
    ::unlink(p.c_str());
#endif
  }
}

Stat LinuxFS::stat(const String &path, i32 depth, i32 maxChildren) {
  Stat s;
  s.path = path;
  String p = resolve(path);
  struct stat st;
#if defined(_WIN32)
  if (::stat(p.c_str(), &st) == 0) {
    s.size = (usz)st.st_size;
    s.isFile = (st.st_mode & _S_IFREG);
    s.isDir = (st.st_mode & _S_IFDIR);
    s.isReadOnly = !(st.st_mode & _S_IWRITE);
#else
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
#endif

    if (s.isDir && depth > 0) {
      DIR *d = opendir(p.c_str());
      if (d) {
        struct dirent *de;
        int count = 0;
        while ((de = readdir(d)) && (maxChildren == 0 || count < maxChildren)) {
          if (strcmp(de->d_name, ".") == 0 || strcmp(de->d_name, "..") == 0)
            continue;
          s.children.push(
              stat(path + "/" + de->d_name, depth - 1, maxChildren));
          count++;
        }
        closedir(d);
      }
    }
  }
  return s;
}

FilesystemDevice *requestFS() {
#ifdef _WIN32
  return new WindowsFS();
#else
  return new LinuxFS();
#endif
}

} // namespace Xi
