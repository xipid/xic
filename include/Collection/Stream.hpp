/**
 * @file Stream.hpp
 * @brief Virtual stream abstractions and C++20 stream concepts for the Xi
 * framework.
 */

#ifndef XI_CORE_STREAM_HPP
#define XI_CORE_STREAM_HPP

#include "../Xi/Func.hpp"
#include "Array.hpp"

namespace Collection {

/**
 * @class VirtualStream
 * @brief Base class for polymorphic data streams.
 */
template <typename T> class VirtualStream {
public:
  /** @brief Called when data is pushed to the stream. Optimization for
   * listeners. */
  Xi::Func<void(const T &)> onPush;

  virtual ~VirtualStream() = default;

  /** @brief Pushes an element to the end of the stream. */
  virtual void push(const T &val) = 0;

  /** @brief Prepends an element to the beginning of the stream. */
  virtual void unshift(const T &val) = 0;

  /** @brief Returns the current number of elements in the stream. */
  virtual usz size() const = 0;

  /** @brief Removes and returns the first element. */
  virtual T shift() = 0;

  /** @brief Removes and returns the last element. */
  virtual T pop() = 0;

  /** @brief Removes a range of elements. */
  virtual void splice(usz start, usz length) = 0;

  /** @brief Cleans up resources. */
  virtual void destroy() = 0;

  // --- Iterators for Generator Support ---
  struct Iterator {
    VirtualStream<T> *stream;
    bool operator!=(const Iterator &other) const { return stream != other.stream; }
    Iterator &operator++() { return *this; }
    T operator*() { return stream->shift(); }
  };

  Iterator begin() { return {this}; }
  Iterator end() { return {null}; }
};

#if __cplusplus >= 202002L
/**
 * @brief C++20 concept for types that behave like streams.
 * Accepts Array, InlineArray, and VirtualStream.
 */
template <typename S, typename T>
concept Stream = requires(S s, T v) {
  { s.push(v) };
  { s.unshift(v) };
  { s.size() } -> std::convertible_to<usz>;
  { s.shift() } -> std::convertible_to<T>;
  { s.pop() } -> std::convertible_to<T>;
};
#endif

} // namespace Collection

#endif // XI_CORE_STREAM_HPP

