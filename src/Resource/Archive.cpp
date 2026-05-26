/**
 * @file Archive.cpp
 * @brief In-memory virtual filesystem implementation for the Xi framework.
 *
 * Always compiled — no external dependencies.
 */

#include "../../include/Resource/Archive.hpp"

namespace Resource {

// -------------------------------------------------------------------------
// Path normalization
// -------------------------------------------------------------------------

String Archive::_normalize(const String &path) const {
  if (path.isEmpty())
    return "/";

  // Build a cleaned path: forward slashes, no double slashes, no trailing /
  String out;
  const u8 *d = path.data();
  usz len = path.size();
  bool lastSlash = false;

  // Ensure leading /
  if (d[0] != '/')
    out.push('/');

  for (usz i = 0; i < len; ++i) {
    char c = (char)d[i];
    if (c == '\\')
      c = '/';
    if (c == '/') {
      if (lastSlash)
        continue;
      lastSlash = true;
    } else {
      lastSlash = false;
    }
    out.push((u8)c);
  }

  // Remove trailing slash (unless root)
  if (out.size() > 1 && out.data()[out.size() - 1] == '/')
    out.allocate(out.size() - 1);

  return out;
}

void Archive::_ensureParents(const String &path) {
  // Walk the path and create directory entries for each component
  const u8 *d = path.data();
  usz len = path.size();

  for (usz i = 1; i < len; ++i) {
    if (d[i] == '/') {
      String parent = path.substring(0, i);
      if (!_entries.has(parent)) {
        VFSEntry e;
        e.isDir = true;
        e.lastAccess = (u64)millis();
        _entries.set(parent, e);
      }
    }
  }
}

void Archive::_touch(VFSEntry &e) { e.lastAccess = (u64)millis(); }

// -------------------------------------------------------------------------
// Lifecycle
// -------------------------------------------------------------------------

Archive::Archive() {
  name = "Archive";
  // Create root directory
  VFSEntry root;
  root.isDir = true;
  root.lastAccess = (u64)millis();
  _entries.set("/", root);
}

Archive::~Archive() {}

// -------------------------------------------------------------------------
// FilesystemDevice interface
// -------------------------------------------------------------------------

String Archive::read(const String &path, u64 startPos, u64 maxLength) {
  String p = _normalize(path);
  VFSEntry *e = _entries.get(p);
  if (!e || e->isDir)
    return String();

  _touch(*e);

  if (!e->cached)
    return String();

  const String &content = e->content;
  if (startPos >= (u64)content.size())
    return String();

  usz avail = content.size() - (usz)startPos;
  usz len = (maxLength > 0 && maxLength < (u64)avail) ? (usz)maxLength : avail;
  return content.substring((usz)startPos, (usz)startPos + len);
}

void Archive::write(const String &path, const String &content, i64 startPos) {
  String p = _normalize(path);
  _ensureParents(p);

  VFSEntry *existing = _entries.get(p);
  if (existing && existing->isDir)
    return; // Can't overwrite a directory

  // Remove old content from cache accounting
  if (existing && existing->cached) {
    _cacheUsed -= existing->content.size();
  }

  VFSEntry e;
  e.isDir = false;
  e.cached = true;
  e.lastAccess = (u64)millis();

  if (startPos <= 0) {
    e.content = content;
  } else {
    // Partial write: keep existing content, overwrite at startPos
    if (existing && existing->cached) {
      e.content = existing->content;
    }
    // Ensure content is large enough
    if ((usz)startPos + content.size() > e.content.size())
      e.content.allocate((usz)startPos + content.size());
    // Copy new content at offset
    u8 *dst = e.content.data() + (usz)startPos;
    const u8 *src = content.data();
    for (usz i = 0; i < content.size(); ++i)
      dst[i] = src[i];
  }

  _cacheUsed += e.content.size();
  _entries.set(p, e);

  if (maxCache > 0)
    evict();
}

void Archive::append(const String &path, const String &content) {
  String p = _normalize(path);
  VFSEntry *existing = _entries.get(p);

  if (existing && existing->cached) {
    _cacheUsed -= existing->content.size();
    existing->content += content;
    _cacheUsed += existing->content.size();
    _touch(*existing);
  } else {
    // File doesn't exist or isn't cached — create it
    write(p, content, 0);
    return;
  }

  if (maxCache > 0)
    evict();
}

void Archive::mkdir(const String &path) {
  String p = _normalize(path);
  if (_entries.has(p))
    return;

  _ensureParents(p);

  VFSEntry e;
  e.isDir = true;
  e.lastAccess = (u64)millis();
  _entries.set(p, e);
}

void Archive::unlink(const String &path) {
  String p = _normalize(path);
  VFSEntry *e = _entries.get(p);
  if (!e)
    return;

  if (e->isDir) {
    // Remove all children recursively
    // Collect keys to remove (can't mutate while iterating)
    Array<String> toRemove;
    String prefix = p + "/";
    for (auto &kv : _entries) {
      if (kv.key.size() > prefix.size()) {
        // Check if kv.key starts with prefix
        bool match = true;
        if (kv.key.size() < prefix.size()) {
          match = false;
        } else {
          for (usz i = 0; i < prefix.size(); ++i) {
            if (kv.key.data()[i] != prefix.data()[i]) {
              match = false;
              break;
            }
          }
        }
        if (match) {
          if (kv.value.cached)
            _cacheUsed -= kv.value.content.size();
          toRemove.push(kv.key);
        }
      }
    }
    for (usz i = 0; i < toRemove.size(); ++i)
      _entries.remove(toRemove[i]);
  } else {
    if (e->cached)
      _cacheUsed -= e->content.size();
  }

  _entries.remove(p);
}

Stat Archive::stat(const String &path, i32 depth, i32 maxChildren) {
  Stat s;
  String p = _normalize(path);
  VFSEntry *e = _entries.get(p);

  if (!e) {
    // Path doesn't exist
    return s;
  }

  s.path = p;
  s.isDir = e->isDir;
  s.isFile = !e->isDir;
  s.size = e->isDir ? 0 : e->content.size();
  s.isReadableByOwner = true;
  s.isWritableByOwner = true;

  if (e->isDir && depth > 0) {
    String prefix = (p == "/") ? "/" : p + "/";
    i32 childCount = 0;
    for (auto &kv : _entries) {
      if (maxChildren > 0 && childCount >= maxChildren)
        break;

      // Check if immediate child (starts with prefix, no further /)
      if (kv.key.size() <= prefix.size())
        continue;

      bool match = true;
      for (usz i = 0; i < prefix.size(); ++i) {
        if (kv.key.data()[i] != prefix.data()[i]) {
          match = false;
          break;
        }
      }
      if (!match)
        continue;

      // Check no further / after prefix (immediate child only)
      bool immediate = true;
      for (usz i = prefix.size(); i < kv.key.size(); ++i) {
        if (kv.key.data()[i] == '/') {
          immediate = false;
          break;
        }
      }
      if (!immediate)
        continue;

      Stat child = stat(kv.key, depth - 1, maxChildren);
      s.children.push(child);
      childCount++;
    }
  }

  return s;
}

SockBind *Archive::socket(const String &) { return nullptr; }
SockStation *Archive::station(const String &) { return nullptr; }

bool Archive::exists(const String &path) const {
  String p = _normalize(path);
  return _entries.has(p);
}

Array<String> Archive::list(const String &path) const {
  Array<String> result;
  String p = _normalize(path);
  String prefix = (p == "/") ? "/" : p + "/";

  for (auto &kv : _entries) {
    if (kv.key.size() <= prefix.size())
      continue;

    // starts with prefix?
    bool match = true;
    for (usz i = 0; i < prefix.size(); ++i) {
      if (kv.key.data()[i] != prefix.data()[i]) {
        match = false;
        break;
      }
    }
    if (!match)
      continue;

    // immediate child?
    bool immediate = true;
    for (usz i = prefix.size(); i < kv.key.size(); ++i) {
      if (kv.key.data()[i] == '/') {
        immediate = false;
        break;
      }
    }
    if (!immediate)
      continue;

    // Extract just the filename part
    result.push(kv.key.substring(prefix.size()));
  }

  return result;
}

// -------------------------------------------------------------------------
// LRU Eviction
// -------------------------------------------------------------------------

void Archive::evict() {
  while (_cacheUsed > maxCache) {
    // Find the least recently accessed cached file
    String *oldest = nullptr;
    u64 oldestTime = ~(u64)0;

    for (auto &kv : _entries) {
      if (!kv.value.isDir && kv.value.cached &&
          kv.value.lastAccess < oldestTime) {
        oldestTime = kv.value.lastAccess;
        oldest = &kv.key;
      }
    }

    if (!oldest)
      break; // Nothing left to evict

    VFSEntry *e = _entries.get(*oldest);
    if (e) {
      _cacheUsed -= e->content.size();
      e->content = String(); // Free the content memory
      e->cached = false;
    }
  }
}

} // namespace Resource
