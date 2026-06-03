/**
 * @file LZ4.cpp
 * @brief LZ4 fast compression — optimized for minimum cycles.
 *
 * Only compiled when XI_LZ4_ENABLED is defined by CMake (lz4 found).
 *
 * Header format: 8-byte LE original size prefix (handles >4GB inputs).
 * Compress: LZ4_compress_fast with acceleration=1 (fastest).
 * Decompress: single-shot LZ4_decompress_safe (size is known from header).
 */

#include "../../include/Resource/Compression.hpp"

#ifdef XI_LZ4_ENABLED

#include <lz4.h>

namespace Resource {

LZ4::LZ4() { level = 1; }

String LZ4::compress(const String &input) {
  if (input.isEmpty())
    return String();

  int srcSize = (int)input.size();
  int bound = LZ4_compressBound(srcSize);
  if (bound <= 0 || (usz)bound + 8 > maxScratch)
    return String();

  String output;
  output.allocate((usz)bound + 8);

  // 8-byte LE size header (future-proof for large buffers)
  u8 *hdr = output.data();
  u64 origSize64 = (u64)input.size();
  hdr[0] = (u8)(origSize64);
  hdr[1] = (u8)(origSize64 >> 8);
  hdr[2] = (u8)(origSize64 >> 16);
  hdr[3] = (u8)(origSize64 >> 24);
  hdr[4] = (u8)(origSize64 >> 32);
  hdr[5] = (u8)(origSize64 >> 40);
  hdr[6] = (u8)(origSize64 >> 48);
  hdr[7] = (u8)(origSize64 >> 56);

  // LZ4_compress_fast with acceleration=1 is the fastest mode
  int compressedSize =
      LZ4_compress_fast((const char *)input.data(),
                         (char *)(output.data() + 8), srcSize, bound, 1);
  if (compressedSize <= 0)
    return String();

  output.allocate((usz)compressedSize + 8);
  return output;
}

String LZ4::decompress(const String &input) {
  if (input.size() < 8)
    return String();

  // Read 8-byte LE original size
  const u8 *hdr = input.data();
  u64 origSize64 = (u64)hdr[0] | ((u64)hdr[1] << 8) | ((u64)hdr[2] << 16) |
                   ((u64)hdr[3] << 24) | ((u64)hdr[4] << 32) |
                   ((u64)hdr[5] << 40) | ((u64)hdr[6] << 48) |
                   ((u64)hdr[7] << 56);

  if (origSize64 == 0 || origSize64 > (u64)maxScratch)
    return String();

  int origSize = (int)origSize64;

  String output;
  output.allocate((usz)origSize);

  int decompressed =
      LZ4_decompress_safe((const char *)(input.data() + 8),
                           (char *)output.data(), (int)(input.size() - 8),
                           origSize);
  if (decompressed < 0)
    return String();

  output.allocate((usz)decompressed);
  return output;
}

} // namespace Resource

#endif // XI_LZ4_ENABLED
