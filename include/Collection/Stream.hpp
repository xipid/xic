/**
 * @file Stream.hpp
 * @brief Virtual stream abstractions and concrete stream implementations for the Xi framework.
 */

#ifndef XI_CORE_STREAM_HPP
#define XI_CORE_STREAM_HPP

#include "../Xi/Func.hpp"
#include "InlineArray.hpp"

namespace Collection {

/**
 * @class StreamBase
 * @brief Non-templated base class for all polymorphic streams.
 */
class StreamBase {
public:
  virtual ~StreamBase() = default;
  virtual usz size() const = 0;
  virtual usz isize() const = 0;
};

/**
 * @class Stream
 * @brief Concrete data stream with yield (T) and inverse (I) channels.
 */
template <typename T, typename I = T> class Stream : public StreamBase {
public:
  InlineArray<T> primary;
  InlineArray<I> inverse;
  Xi::Func<void(const T &)> onPush;

  Stream() = default;
  virtual ~Stream() = default;

  // --- Primary Channel (Yield) ---
  virtual void push(const T &val) { 
    primary.push(val); 
    if (onPush) onPush(val); 
  }
  
  virtual void unshift(const T &val) { primary.unshift(val); }
  
  virtual T shift() { return primary.shift(); }
  
  virtual T pop() { return primary.pop(); }
  
  usz size() const override { return primary.size(); }

  T &operator[](usz idx) { return primary[idx]; }
  const T &operator[](usz idx) const { return primary[idx]; }

  // --- Inverse Channel ---
  virtual void ipush(const I &val) { inverse.push(val); }
  
  virtual void iunshift(const I &val) { inverse.unshift(val); }
  
  virtual I ishift() { return inverse.shift(); }
  
  virtual I ipop() { return inverse.pop(); }
  
  usz isize() const override { return inverse.size(); }

  // --- General ---
  virtual void splice(usz start, usz length) { primary.splice(start, length); }
  
  virtual void destroy() {
    primary.destroy();
    inverse.destroy();
  }

  // --- Iterators for Generator Support ---
  struct Iterator {
    Stream<T, I> *stream;
    bool operator!=(const Iterator &other) const { return stream != other.stream; }
    Iterator &operator++() { return *this; }
    T operator*() { return stream->shift(); }
  };

  Iterator begin() { return {this}; }
  Iterator end() { return {nullptr}; }
};

/**
 * @class VirtualStream
 * @brief Alias for backward compatibility.
 */
template <typename T> using VirtualStream = Stream<T, T>;

} // namespace Collection

#endif // XI_CORE_STREAM_HPP
