/**
 * @file ZIPArchive.cpp
 * @brief ZIP format virtual filesystem using miniz for the Xi framework.
 *
 * Only compiled when XI_DEFLATE_ENABLED is defined by CMake (miniz found).
 *
 * Design:
 *   - load() parses the ZIP central directory into _zipIndex.
 *   - read() checks cache first; on miss, decompresses the entry lazily
 *     using raw ZIP data from _rawZip or the onFormatRequest callback.
 *   - formatCompressed() serializes the entire virtual FS back to ZIP.
 *   - formatRead() provides raw block access into the ZIP blob.
 */

#include "../../include/Resource/Archive.hpp"

#ifdef XI_DEFLATE_ENABLED

#include <cstring>
#include <miniz.h>

namespace Resource {

// -------------------------------------------------------------------------
// Lifecycle
// -------------------------------------------------------------------------

ZIPArchive::ZIPArchive() { name = "ZIPArchive"; }
ZIPArchive::~ZIPArchive() {}

void ZIPArchive::onFormatRequest(Func<String(u64, u64)> cb) {
  _formatRequestCb = Move(cb);
}

// -------------------------------------------------------------------------
// Load — parse existing ZIP into the virtual FS
// -------------------------------------------------------------------------

bool ZIPArchive::load(const String &zipData) {
  _rawZip = zipData;
  _zipIndex.clear();

  return _parseDirectory();
}

bool ZIPArchive::_parseDirectory() {
  if (_rawZip.isEmpty())
    return false;

  mz_zip_archive zip;
  memset(&zip, 0, sizeof(zip));

  if (!mz_zip_reader_init_mem(&zip, _rawZip.data(), _rawZip.size(), 0))
    return false;

  mz_uint numFiles = mz_zip_reader_get_num_files(&zip);

  for (mz_uint i = 0; i < numFiles; ++i) {
    mz_zip_archive_file_stat fstat;
    if (!mz_zip_reader_file_stat(&zip, i, &fstat))
      continue;

    String filename(fstat.m_filename);

    if (mz_zip_reader_is_file_a_directory(&zip, i)) {
      mkdir(filename);
    } else {
      // Store ZIP metadata for lazy decompression
      ZIPFileInfo info;
      info.localHeaderOffset = fstat.m_local_header_ofs;
      info.compressedSize = fstat.m_comp_size;
      info.uncompressedSize = fstat.m_uncomp_size;
      info.compressionMethod = (u16)fstat.m_method;
      _zipIndex.set(filename, info);

      // Create a metadata-only entry (not cached — content loaded on demand)
      String p = _normalize(filename);
      _ensureParents(p);

      VFSEntry e;
      e.isDir = false;
      e.cached = false;
      e.lastAccess = (u64)millis();
      _entries.set(p, e);
    }
  }

  mz_zip_reader_end(&zip);
  return true;
}

// -------------------------------------------------------------------------
// Lazy decompression
// -------------------------------------------------------------------------

String ZIPArchive::_decompressEntry(const ZIPFileInfo &info) {
  // Get the raw ZIP data for this entry
  // We need from the local header through the compressed data
  const u8 *zipBase = nullptr;
  usz zipSize = 0;
  String fetchedBlock;

  if (_rawZip.size() > 0) {
    zipBase = _rawZip.data();
    zipSize = _rawZip.size();
  } else if (_formatRequestCb.isValid()) {
    // Fetch the entire ZIP via callback (or at least enough)
    // For simplicity, fetch from 0 to the end of this entry's data
    u64 endOffset = info.localHeaderOffset + 30 + 256 + info.compressedSize;
    fetchedBlock = _formatRequestCb(0, endOffset);
    if (fetchedBlock.isEmpty())
      return String();
    zipBase = fetchedBlock.data();
    zipSize = fetchedBlock.size();
  } else {
    return String();
  }

  if (info.compressionMethod == 0) {
    // Stored (no compression)
    // Parse the local file header to find data offset
    if (info.localHeaderOffset + 30 > zipSize)
      return String();

    const u8 *lh = zipBase + info.localHeaderOffset;
    u16 fnLen = (u16)lh[26] | ((u16)lh[27] << 8);
    u16 exLen = (u16)lh[28] | ((u16)lh[29] << 8);
    u64 dataOffset = info.localHeaderOffset + 30 + fnLen + exLen;

    if (dataOffset + info.uncompressedSize > zipSize)
      return String();

    String result;
    result.allocate((usz)info.uncompressedSize);
    const u8 *src = zipBase + dataOffset;
    u8 *dst = result.data();
    for (usz i = 0; i < (usz)info.uncompressedSize; ++i)
      dst[i] = src[i];
    return result;
  }

  if (info.compressionMethod == 8) {
    // Deflate — use miniz mz_zip_reader to decompress
    mz_zip_archive zip;
    memset(&zip, 0, sizeof(zip));

    if (!mz_zip_reader_init_mem(&zip, zipBase, zipSize, 0))
      return String();

    // Find the file index by local header offset
    mz_uint numFiles = mz_zip_reader_get_num_files(&zip);
    for (mz_uint i = 0; i < numFiles; ++i) {
      mz_zip_archive_file_stat fs;
      if (!mz_zip_reader_file_stat(&zip, i, &fs))
        continue;
      if ((u64)fs.m_local_header_ofs == info.localHeaderOffset) {
        usz outSize = (usz)info.uncompressedSize;
        String result;
        result.allocate(outSize);

        if (mz_zip_reader_extract_to_mem(&zip, i, result.data(), outSize, 0)) {
          mz_zip_reader_end(&zip);
          return result;
        }
        break;
      }
    }

    mz_zip_reader_end(&zip);
  }

  return String();
}

String ZIPArchive::read(const String &path, u64 startPos, u64 maxLength) {
  String p = _normalize(path);

  // Check if cached in the Archive base
  VFSEntry *e = _entries.get(p);
  if (!e || e->isDir)
    return String();

  if (e->cached) {
    _touch(*e);
    const String &content = e->content;
    if (startPos >= (u64)content.size())
      return String();
    usz avail = content.size() - (usz)startPos;
    usz len =
        (maxLength > 0 && maxLength < (u64)avail) ? (usz)maxLength : avail;
    return content.substring((usz)startPos, (usz)startPos + len);
  }

  // Cache miss — try to decompress from ZIP index
  // Lookup with both normalized and original-style paths
  ZIPFileInfo *info = _zipIndex.get(p);
  // Also try without leading slash
  if (!info && p.size() > 1 && p.data()[0] == '/')
    info = _zipIndex.get(p.substring(1));

  if (!info) {
    // Try the onFormatRequest callback as a fallback
    if (_formatRequestCb.isValid()) {
      // Can't decompress without ZIP metadata
    }
    return String();
  }

  // Decompress
  String content = _decompressEntry(*info);
  if (content.isEmpty())
    return String();

  // Cache the decompressed content
  e->content = content;
  e->cached = true;
  _cacheUsed += content.size();
  _touch(*e);

  if (maxCache > 0)
    evict();

  if (startPos >= (u64)content.size())
    return String();
  usz avail = content.size() - (usz)startPos;
  usz len = (maxLength > 0 && maxLength < (u64)avail) ? (usz)maxLength : avail;
  return content.substring((usz)startPos, (usz)startPos + len);
}

// -------------------------------------------------------------------------
// Format access — raw ZIP blob operations
// -------------------------------------------------------------------------

usz ZIPArchive::formatSize() const { return _rawZip.size(); }

String ZIPArchive::formatRead(u64 position, u64 length) {
  if (_rawZip.size() > 0) {
    if (position >= (u64)_rawZip.size())
      return String();
    usz avail = _rawZip.size() - (usz)position;
    usz len = (length > 0 && (usz)length < avail) ? (usz)length : avail;
    return _rawZip.substring((usz)position, (usz)position + len);
  }

  // Delegate to callback
  if (_formatRequestCb.isValid())
    return _formatRequestCb(position, length);

  return String();
}

String ZIPArchive::formatCompressed() {
  mz_zip_archive zip;
  memset(&zip, 0, sizeof(zip));

  if (!mz_zip_writer_init_heap(&zip, 0, 0))
    return String();

  // Walk all entries, add files
  for (auto &kv : _entries) {
    String filepath = kv.key;
    VFSEntry &e = kv.value;

    if (e.isDir || filepath == "/")
      continue;

    // Remove leading slash for ZIP convention
    if (filepath.size() > 0 && filepath.data()[0] == '/')
      filepath = filepath.substring(1);

    if (e.cached) {
      mz_zip_writer_add_mem(&zip, (const char *)filepath.c_str(),
                            e.content.data(), e.content.size(),
                            MZ_DEFAULT_COMPRESSION);
    } else {
      ZIPFileInfo *info = _zipIndex.get(kv.key);
      if (info) {
        String content = _decompressEntry(*info);
        if (content.size() > 0) {
          mz_zip_writer_add_mem(&zip, (const char *)filepath.c_str(),
                                content.data(), content.size(),
                                MZ_DEFAULT_COMPRESSION);
        }
      }
    }
  }

  // Extract the heap buffer
  void *buf = nullptr;
  size_t bufSize = 0;
  
  // In miniz 3.x, finalize_heap_archive handles everything
  mz_zip_writer_finalize_heap_archive(&zip, &buf, &bufSize);
  mz_zip_writer_end(&zip);

  if (!buf || bufSize == 0) {
    return String();
  }

  String result((const u8 *)buf, (usz)bufSize);
  mz_free(buf);

  _rawZip = result;
  return result;
}

} // namespace Resource

#endif // XI_DEFLATE_ENABLED
