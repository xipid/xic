/**
 * @file Deflate.cpp
 * @brief DEFLATE compression using miniz — optimized for speed.
 *
 * Only compiled when XI_DEFLATE_ENABLED is defined by CMake (miniz found).
 *
 * Compress: single-shot mz_compress2 (already optimal for zlib).
 * Decompress: streaming tinfl via mz_stream to avoid restarting from scratch
 *             when the output buffer needs to grow.
 */

#include "../../include/Resource/Compression.hpp"

#ifdef XI_DEFLATE_ENABLED

#include <miniz.h>
#include <cstring>

namespace Resource {

DEFLATE::DEFLATE() { level = 6; }

String DEFLATE::compress(const String &input) {
  if (input.isEmpty())
    return String();

  mz_ulong bound = mz_compressBound((mz_ulong)input.size());
  if ((usz)bound > maxScratch)
    return String();

  String output;
  output.allocate((usz)bound);

  mz_ulong destLen = bound;
  int ret = mz_compress2(output.data(), &destLen, input.data(),
                          (mz_ulong)input.size(), level);
  if (ret != MZ_OK)
    return String();

  output.allocate((usz)destLen);
  return output;
}

String DEFLATE::decompress(const String &input) {
  if (input.isEmpty())
    return String();

  // Use streaming decompression so we never re-decompress already-processed
  // bytes when the output buffer needs to grow.
  mz_stream stream;
  memset(&stream, 0, sizeof(stream));

  int ret = mz_inflateInit(&stream);
  if (ret != MZ_OK)
    return String();

  stream.next_in = input.data();
  stream.avail_in = (unsigned int)input.size();

  // Initial output guess
  usz outCap = input.size() * 4;
  if (outCap < 256)
    outCap = 256;
  if (outCap > maxScratch)
    outCap = maxScratch;

  String output;
  output.allocate(outCap);
  usz totalOut = 0;

  for (;;) {
    stream.next_out = output.data() + totalOut;
    stream.avail_out = (unsigned int)(outCap - totalOut);

    ret = mz_inflate(&stream, MZ_NO_FLUSH);

    totalOut = (usz)stream.total_out;

    if (ret == MZ_STREAM_END)
      break;

    if (ret != MZ_OK) {
      mz_inflateEnd(&stream);
      return String();
    }

    // Need more output space
    if (stream.avail_out == 0) {
      if (outCap >= maxScratch) {
        mz_inflateEnd(&stream);
        return String();
      }
      outCap *= 2;
      if (outCap > maxScratch)
        outCap = maxScratch;
      output.allocate(outCap);
    }
  }

  mz_inflateEnd(&stream);
  output.allocate(totalOut);
  return output;
}

} // namespace Resource

#endif // XI_DEFLATE_ENABLED
