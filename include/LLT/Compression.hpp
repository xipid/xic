/**
 * @file Compression.hpp
 * @brief Abstract compression interface and concrete backends for Xi.
 *
 * Compression is the abstract base. Concrete backends (DEFLATE, LZ4, ZSTD)
 * are conditionally compiled when their respective library headers are found:
 *   - DEFLATE: miniz.h  → XI_DEFLATE_ENABLED
 *   - LZ4:    lz4.h     → XI_LZ4_ENABLED
 *   - ZSTD:   zstd.h    → XI_ZSTD_ENABLED
 */

#ifndef XI_LLT_COMPRESSION_HPP
#define XI_LLT_COMPRESSION_HPP

#include "../Collection/Array.hpp"
#include "../Collection/String.hpp"

namespace LLT {

using namespace Xi;
using namespace Collection;

/**
 * @class Compression
 * @brief Abstract base class for lossless compression algorithms.
 *
 * Provides a dictionary-learning interface: the engine can accumulate
 * recurring patterns and exploit them across subsequent compress/decompress
 * calls. `maxScratch` caps the working-memory budget.
 */
class XI_EXPORT Compression {
public:
  usz maxScratch = 1024 * 1024 * 128; ///< Max scratch memory (default 128 MB).
  Array<String> dictionary;           ///< Learned dictionary entries.
  int level = 6;                      ///< Compression level (0-9 or backend-specific).

  Compression() = default;
  virtual ~Compression() = default;

  /**
   * @brief Compresses input data.
   * @param input Raw data to compress.
   * @return Compressed data, or empty String on failure.
   */
  virtual String compress(const String &input) = 0;

  /**
   * @brief Decompresses previously compressed data.
   * @param input Compressed data.
   * @return Decompressed data, or empty String on failure.
   */
  virtual String decompress(const String &input) = 0;
};

// =========================================================================
// DEFLATE (miniz)
// =========================================================================

#ifdef XI_DEFLATE_ENABLED

/**
 * @class DEFLATE
 * @brief DEFLATE/zlib compression using miniz.
 */
class XI_EXPORT DEFLATE : public Compression {
public:
  DEFLATE();
  ~DEFLATE() override = default;

  String compress(const String &input) override;
  String decompress(const String &input) override;
};

#endif // XI_DEFLATE_ENABLED

// =========================================================================
// LZ4
// =========================================================================

#ifdef XI_LZ4_ENABLED

/**
 * @class LZ4
 * @brief LZ4 fast compression.
 */
class XI_EXPORT LZ4 : public Compression {
public:
  LZ4();
  ~LZ4() override = default;

  String compress(const String &input) override;
  String decompress(const String &input) override;
};

#endif // XI_LZ4_ENABLED

// =========================================================================
// ZSTD
// =========================================================================

#ifdef XI_ZSTD_ENABLED

/**
 * @class ZSTD
 * @brief Zstandard compression with persistent context reuse.
 *
 * CCtx and DCtx are allocated once and reused across all compress/decompress
 * calls. CDict/DDict are rebuilt lazily when the dictionary blob changes.
 * Contexts and dictionaries are stored as void* to avoid exposing zstd.h
 * in the header.
 */
class XI_EXPORT ZSTD : public Compression {
public:
  ZSTD();
  ~ZSTD() override;

  /**
   * @brief Trains a dictionary from sample data.
   * @param samples Array of sample data buffers.
   * @param dictSize Target dictionary size in bytes (default 112KB).
   */
  void train(const Array<String> &samples, usz dictSize = 112 * 1024);

  /**
   * @brief Sets a pre-trained dictionary and flags it as dirty.
   */
  void setDictionary(const String &dict);

  String compress(const String &input) override;
  String decompress(const String &input) override;

private:
  void *_cctx = nullptr;  ///< ZSTD_CCtx* (persistent).
  void *_dctx = nullptr;  ///< ZSTD_DCtx* (persistent).
  void *_cdict = nullptr; ///< ZSTD_CDict* (rebuilt on dict change).
  void *_ddict = nullptr; ///< ZSTD_DDict* (rebuilt on dict change).
  bool _dictDirty = false; ///< True when dictionary changed since last build.

  void _rebuildDicts();
  void _freeDicts();
};

#endif // XI_ZSTD_ENABLED

} // namespace LLT

#endif // XI_LLT_COMPRESSION_HPP
