/**
 * @file Zstd.cpp
 * @brief Zstandard compression — zero-alloc hot path via persistent contexts.
 *
 * Only compiled when XI_ZSTD_ENABLED is defined by CMake (libzstd found).
 *
 * Design:
 *   - CCtx and DCtx are allocated once in the constructor and reused.
 *   - CDict/DDict are rebuilt lazily only when the dictionary blob changes.
 *   - Decompression uses frame content size when available; otherwise grows
 *     the output buffer on ZSTD_error_dstSize_tooSmall until maxScratch.
 */

#include "../../include/LLT/Compression.hpp"

#ifdef XI_ZSTD_ENABLED

#include <zstd.h>
#if __has_include(<zdict.h>)
#include <zdict.h>
#define XI_ZSTD_DICT_AVAILABLE 1
#endif

namespace LLT {

// -------------------------------------------------------------------------
// Lifecycle
// -------------------------------------------------------------------------

ZSTD::ZSTD() {
  level = 3;
  _cctx = (void *)ZSTD_createCCtx();
  _dctx = (void *)ZSTD_createDCtx();
}

ZSTD::~ZSTD() {
  _freeDicts();
  if (_cctx)
    ZSTD_freeCCtx((ZSTD_CCtx *)_cctx);
  if (_dctx)
    ZSTD_freeDCtx((ZSTD_DCtx *)_dctx);
}

// -------------------------------------------------------------------------
// Dictionary management
// -------------------------------------------------------------------------

void ZSTD::_freeDicts() {
  if (_cdict) {
    ZSTD_freeCDict((ZSTD_CDict *)_cdict);
    _cdict = nullptr;
  }
  if (_ddict) {
    ZSTD_freeDDict((ZSTD_DDict *)_ddict);
    _ddict = nullptr;
  }
}

void ZSTD::_rebuildDicts() {
  _freeDicts();
  if (dictionary.size() > 0 && dictionary[0].size() > 0) {
    const u8 *d = dictionary[0].data();
    usz ds = dictionary[0].size();
    _cdict = (void *)ZSTD_createCDict(d, ds, level);
    _ddict = (void *)ZSTD_createDDict(d, ds);
  }
  _dictDirty = false;
}

void ZSTD::train(const Array<String> &samples, usz dictSize) {
#ifdef XI_ZSTD_DICT_AVAILABLE
  if (samples.size() == 0)
    return;

  // Concatenate samples + build size table
  usz totalSize = 0;
  for (usz i = 0; i < samples.size(); ++i)
    totalSize += samples[i].size();

  String concat;
  concat.allocate(totalSize);
  Array<usz> sizes;
  sizes.allocate(samples.size());

  usz off = 0;
  for (usz i = 0; i < samples.size(); ++i) {
    const u8 *src = samples[i].data();
    u8 *dst = concat.data() + off;
    usz n = samples[i].size();
    for (usz j = 0; j < n; ++j)
      dst[j] = src[j];
    sizes[i] = n;
    off += n;
  }

  if (dictSize > maxScratch)
    dictSize = maxScratch;

  String dictBuf;
  dictBuf.allocate(dictSize);

  usz result = ZDICT_trainFromBuffer(dictBuf.data(), dictSize, concat.data(),
                                     sizes.data(), (unsigned)samples.size());
  if (ZSTD_isError(result)) {
    dictionary.clear();
    _dictDirty = true;
    return;
  }

  dictBuf.allocate(result);
  dictionary.clear();
  dictionary.push(dictBuf);
  _dictDirty = true;
#else
  (void)samples;
  (void)dictSize;
#endif
}

void ZSTD::setDictionary(const String &dict) {
  dictionary.clear();
  dictionary.push(dict);
  _dictDirty = true;
}

// -------------------------------------------------------------------------
// Compress — hot path: no allocations other than the output String
// -------------------------------------------------------------------------

String ZSTD::compress(const String &input) {
  if (input.isEmpty() || !_cctx)
    return String();

  if (_dictDirty)
    _rebuildDicts();

  usz bound = ZSTD_compressBound(input.size());
  if (bound > maxScratch)
    return String();

  String output;
  output.allocate(bound);

  usz compressedSize;
  ZSTD_CCtx *ctx = (ZSTD_CCtx *)_cctx;

  if (_cdict) {
    compressedSize =
        ZSTD_compress_usingCDict(ctx, output.data(), bound, input.data(),
                                 input.size(), (ZSTD_CDict *)_cdict);
  } else {
    compressedSize = ZSTD_compressCCtx(ctx, output.data(), bound, input.data(),
                                       input.size(), level);
  }

  if (ZSTD_isError(compressedSize))
    return String();

  output.allocate(compressedSize);
  return output;
}

// -------------------------------------------------------------------------
// Decompress — growable buffer, single-shot context reuse
// -------------------------------------------------------------------------

String ZSTD::decompress(const String &input) {
  if (input.isEmpty() || !_dctx)
    return String();

  if (_dictDirty)
    _rebuildDicts();

  // Try to get exact size from frame header — avoids any guessing
  unsigned long long frameSize =
      ZSTD_getFrameContentSize(input.data(), input.size());

  usz outCap;
  bool sizeKnown = false;

  if (frameSize != ZSTD_CONTENTSIZE_UNKNOWN &&
      frameSize != ZSTD_CONTENTSIZE_ERROR) {
    outCap = (usz)frameSize;
    sizeKnown = true;
  } else {
    outCap = input.size() * 4;
    if (outCap < 256)
      outCap = 256;
  }

  if (outCap > maxScratch)
    outCap = maxScratch;

  ZSTD_DCtx *ctx = (ZSTD_DCtx *)_dctx;

  for (;;) {
    String output;
    output.allocate(outCap);

    usz decompressedSize;
    if (_ddict) {
      decompressedSize =
          ZSTD_decompress_usingDDict(ctx, output.data(), outCap, input.data(),
                                     input.size(), (ZSTD_DDict *)_ddict);
    } else {
      decompressedSize = ZSTD_decompressDCtx(ctx, output.data(), outCap,
                                             input.data(), input.size());
    }

    if (!ZSTD_isError(decompressedSize)) {
      output.allocate(decompressedSize);
      return output;
    }

    // If size was known or we hit maxScratch, no point retrying
    if (sizeKnown || outCap >= maxScratch)
      return String();

    // Grow and retry
    outCap *= 2;
    if (outCap > maxScratch)
      outCap = maxScratch;
  }
}

} // namespace LLT

#endif // XI_ZSTD_ENABLED
