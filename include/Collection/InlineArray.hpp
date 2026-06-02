/**
 * @file InlineArray.hpp
 * @brief High-performance contiguous dynamic array with CoW and device support.

 */

#ifndef XI_CORE_INLINE_ARRAY_HPP
#define XI_CORE_INLINE_ARRAY_HPP

#include "../Xi/Primitives.hpp"
#include <cstring>

#if defined(AVR) || defined(ARDUINO)
#define XI_ARRAY_MIN_CAP 4
#else
#define XI_ARRAY_MIN_CAP 16
#endif

using namespace Xi;

namespace Collection {

/**
 * @class InlineArray
 * @brief A high-performance, reference-counted, contiguous dynamic array.
 *
 * Supports Copy-on-Write (CoW), slicing, and multi-dimensional views.
 * Can also represent data resident on hardware devices (GPU/DSP).
 *
 * @tparam T Element type.
 */
template <typename T> class XI_EXPORT InlineArray {
public:
  /**
   * @struct Block
   * @brief Internal control block for memory management and reference counting.
   */
  struct Block {
    usz useCount; ///< Number of InlineArray instances sharing this block.
    usz capacity; ///< Total allocated capacity in elements.
    usz _length;  ///< Actually used length of the allocated memory.

    IMemoryDevice *device =
        nullptr; ///< Pointer to memory device (nullptr for CPU).
    void *deviceHandle = nullptr; ///< Opaque handle for device-resident data.

    /** @brief Returns a pointer to the start of the payload data. */
    T *get_data() {
      usz header = sizeof(Block);
      u8 *base = (u8 *)this;
      u8 *data_start = base + header;
      usz addr = (usz)data_start;
      usz align = alignof(T);
      if ((addr % align) != 0) {
        usz pad = align - (addr % align);
        data_start += pad;
      }
      return (T *)data_start;
    }

    /** @brief Allocates a new CPU-resident block. */
    static Block *allocate(usz cap) {
      usz align = alignof(T);
      if (align < alignof(Block))
        align = alignof(Block);
      usz header_size = sizeof(Block);
      usz worst_padding = align;
      usz payload_size = (cap + 1) * sizeof(T);
      usz total = header_size + worst_padding + payload_size;
      u8 *raw = (u8 *)::operator new(total);
      Block *b = (Block *)raw;
      b->useCount = 1;
      b->capacity = cap;
      b->_length = 0;
      b->device = nullptr;
      b->deviceHandle = nullptr;
      return b;
    }

    /** @brief Allocates a block for device-resident data. */
    static Block *allocateDevice(IMemoryDevice *dev, usz size) {
      usz total = sizeof(Block) + alignof(T);
      u8 *raw = (u8 *)::operator new(total);
      Block *b = (Block *)raw;
      b->useCount = 1;
      b->capacity = 0;
      b->_length = size / sizeof(T);
      b->device = dev;
      b->deviceHandle = dev->alloc(size);
      return b;
    }

    /** @brief Wraps an existing device allocation. */
    static Block *wrapDevice(IMemoryDevice *dev, void *handle, usz count) {
      usz total = sizeof(Block) + alignof(T);
      u8 *raw = (u8 *)::operator new(total);
      Block *b = (Block *)raw;
      b->useCount = 1;
      b->capacity = 0;
      b->_length = count;
      b->device = dev;
      b->deviceHandle = handle;
      return b;
    }

    /** @brief Destroys the block and its contents. */
    static void destroy(Block *b) {
      if (!b)
        return;
      if (b->device && b->deviceHandle) {
        b->device->free(b->deviceHandle);
        b->deviceHandle = nullptr;
      }
      if (!b->device) {
        T *ptr = b->get_data();
        for (usz i = 0; i < b->_length; ++i)
          ptr[i].~T();
      }
      ::operator delete(b);
    }
  };

  Block *block;
  T *_data;    ///< Pointer to the start of this slice.
  usz _length; ///< Number of elements in this slice.
  usz offset;  ///< Global index offset.
  usz *_dims;  ///< Dimensions for multi-dimensional views.
  u8 _rank;    ///< Rank (number of dimensions).

  InlineArray()
      : block(nullptr), _data(nullptr), _length(0), offset(0), _dims(nullptr),
        _rank(1) {}

  ~InlineArray() {
    destroy();
    if (_dims)
      delete[] _dims;
  }

  InlineArray(const InlineArray &other)
      : block(other.block), _data(other._data), _length(other._length),
        offset(other.offset), _dims(nullptr), _rank(other._rank) {
    if (other._dims) {
      _dims = new usz[_rank];
      for (u8 i = 0; i < _rank; i++)
        _dims[i] = other._dims[i];
    }
    retain();
  }

  InlineArray(InlineArray &&other) noexcept
      : block(other.block), _data(other._data), _length(other._length),
        offset(other.offset), _dims(other._dims), _rank(other._rank) {
    other.block = nullptr;
    other._data = nullptr;
    other._length = 0;
    other.offset = 0;
    other._dims = nullptr;
    other._rank = 1;
  }

  InlineArray &operator=(const InlineArray &other) {
    if (this == &other)
      return *this;
    destroy();
    if (_dims) {
      delete[] _dims;
      _dims = nullptr;
    }
    block = other.block;
    _data = other._data;
    _length = other._length;
    offset = other.offset;
    _rank = other._rank;
    if (other._dims) {
      _dims = new usz[_rank];
      for (u8 i = 0; i < _rank; i++)
        _dims[i] = other._dims[i];
    }
    retain();
    return *this;
  }

  InlineArray &operator=(InlineArray &&other) noexcept {
    if (this == &other)
      return *this;
    destroy();
    if (_dims) {
      delete[] _dims;
      _dims = nullptr;
    }
    block = other.block;
    _data = other._data;
    _length = other._length;
    offset = other.offset;
    _dims = other._dims;
    _rank = other._rank;
    other.block = nullptr;
    other._data = nullptr;
    other._length = 0;
    other.offset = 0;
    other._dims = nullptr;
    other._rank = 1;
    return *this;
  }

  /** @brief Configures the multi-dimensional shape of the array. */
  InlineArray &shape(u8 rank, const usz *d) {
    if (_dims) {
      delete[] _dims;
      _dims = nullptr;
    }
    _rank = (rank > 0 ? rank : 1);
    if (d && _rank > 1) {
      _dims = new usz[_rank];
      for (u8 i = 0; i < _rank; i++)
        _dims[i] = d[i];
    }
    return *this;
  }

  u8 rank() const { return _rank; }

  /** @brief Increments the reference count. */
  void retain() {
    if (block)
      block->useCount++;
  }

  /** @brief Decrements the reference count and optionally destroys the block.
   */
  void destroy() {
    if (block) {
      if (block->useCount > 0) {
        block->useCount--;
        if (block->useCount == 0)
          Block::destroy(block);
      }
      block = nullptr;
    }
    _data = nullptr;
    _length = 0;
  }

  /** @brief Allocates or resizes the array. */
  bool allocate(usz len, bool safe = false) {
    if (!block) {
      block = Block::allocate(len);
      T *ptr = block->get_data();
      for (usz i = 0; i < len; ++i)
        new (&ptr[i]) T();
      block->_length = len;
      new (&ptr[len]) T();
      _data = ptr;
      _length = len;
      return true;
    }
    bool is_root = (_data == block->get_data());
    bool unique = (block->useCount == 1);
    if (unique && is_root && len <= block->capacity) {
      if (len > block->_length) {
        T *ptr = block->get_data();
        for (usz i = block->_length; i < len; ++i)
          new (&ptr[i]) T();
      } else if (len < block->_length) {
        T *ptr = block->get_data();
        for (usz i = len; i < block->_length; ++i)
          ptr[i].~T();
      }
      block->_length = len;
      T *ptr = block->get_data();
      new (&ptr[len]) T();
      _length = len;
      return true;
    }
    if (safe)
      return false;
    Block *nb = Block::allocate(len);
    usz copy_cnt = (_length < len) ? _length : len;
    T *src = _data;
    T *dst = nb->get_data();
    for (usz i = 0; i < copy_cnt; ++i)
      new (&dst[i]) T(Xi::Move(src[i]));
    for (usz i = copy_cnt; i < len; ++i)
      new (&dst[i]) T();
    nb->_length = len;
    new (&dst[len]) T();
    destroy();
    block = nb;
    _data = nb->get_data();
    _length = len;
    return true;
  }

  /** @brief Ensures capacity for at least cap elements. */
  bool reserve(usz cap) {
    if (block && cap <= block->capacity)
      return true;
    Block *nb = Block::allocate(cap);
    if (!nb)
      return false;
    if (block) {
      T *src = _data;
      T *dst = nb->get_data();
      usz toCopy = _length < cap ? _length : cap;
      for (usz i = 0; i < toCopy; i++)
        new (&dst[i]) T(Xi::Move(src[i]));
      nb->_length = toCopy;
      destroy();
    }
    block = nb;
    _data = block->get_data();
    _length = block->_length;
    return true;
  }

  T *data() { return _data; }
  const T *data() const { return _data; }
  usz size() const { return _length; }

  T &operator[](usz idx) { return _data[idx]; }
  const T &operator[](usz idx) const { return _data[idx]; }

  bool has(usz idx) const {
    if (idx < offset)
      return false;
    return (idx - offset) < _length;
  }

  long long indexOf(const T &val) const {
    for (usz i = 0; i < _length; ++i) {
      if (Equal<T>::eq(_data[i], val))
        return (long long)(offset + i);
    }
    return -1;
  }

  void push(const T &val) {
    if (!block) {
      block = Block::allocate(XI_ARRAY_MIN_CAP);
      _data = block->get_data();
      _length = 0;
      offset = 0;
    }
    if (block->useCount > 1 || _data != block->get_data() ||
        (_data + _length) != (block->get_data() + block->_length)) {
      usz old_s = _length;
      usz new_cap = (old_s + 1) * 2;
      if (new_cap < XI_ARRAY_MIN_CAP)
        new_cap = XI_ARRAY_MIN_CAP;
      Block *nb = Block::allocate(new_cap);
      T *dst = nb->get_data();
      for (usz i = 0; i < old_s; ++i)
        new (&dst[i]) T(_data[i]);
      new (&dst[old_s]) T(val);
      nb->_length = old_s + 1;
      new (&dst[nb->_length]) T();
      destroy();
      block = nb;
      _data = nb->get_data();
      _length = nb->_length;
      return;
    }
    if (block->_length + 1 > block->capacity) {
      usz new_cap = block->capacity * 2;
      Block *old = block;
      Block *nb = Block::allocate(new_cap);
      T *dst = nb->get_data();
      T *src = _data;
      for (usz i = 0; i < _length; ++i)
        new (&dst[i]) T(Xi::Move(src[i]));
      new (&dst[_length]) T(val);
      nb->_length = _length + 1;
      new (&dst[nb->_length]) T();
      Block::destroy(old);
      block = nb;
      _data = nb->get_data();
      _length = nb->_length;
    } else {
      new (&_data[_length]) T(val);
      block->_length++;
      _length++;
      new (&_data[_length]) T();
    }
  }

  void pushEach(const T *vals, usz count) {
    for (usz i = 0; i < count; ++i)
      push(vals[i]);
  }

  void set(const T *vals, usz count) {
    destroy();
    if (count > 0 && vals)
      pushEach(vals, count);
  }

  T pop() {
    if (_length == 0)
      return T();
    if (block->useCount > 1 || _data != block->get_data()) {
      usz old_s = _length;
      T ret = _data[old_s - 1];
      Block *nb = Block::allocate(old_s - 1);
      T *dst = nb->get_data();
      for (usz i = 0; i < old_s - 1; ++i)
        new (&dst[i]) T(_data[i]);
      nb->_length = old_s - 1;
      new (&dst[nb->_length]) T();
      destroy();
      block = nb;
      _data = nb->get_data();
      _length = nb->_length;
      return ret;
    }
    T ret = Xi::Move(_data[_length - 1]);
    _data[_length - 1].~T();
    _length--;
    block->_length--;
    new (&_data[_length]) T();
    return ret;
  }

  void unshift(const T &val) {
    if (block && (block->useCount > 1 || _data != block->get_data())) {
      usz old_s = _length;
      Block *nb = Block::allocate(old_s + 1);
      T *dst = nb->get_data();
      new (&dst[0]) T(val);
      for (usz i = 0; i < old_s; ++i)
        new (&dst[i + 1]) T(_data[i]);
      nb->_length = old_s + 1;
      new (&dst[nb->_length]) T();
      destroy();
      block = nb;
      _data = nb->get_data();
      _length = nb->_length;
      return;
    }
    if (!block) {
      push(val);
      return;
    }
    if (block->_length < block->capacity) {
      for (usz i = _length; i > 0; --i) {
        new (&_data[i]) T(Xi::Move(_data[i - 1]));
        _data[i - 1].~T();
      }
      new (&_data[0]) T(val);
      block->_length++;
      _length++;
      new (&_data[_length]) T();
    } else {
      usz new_cap = block->capacity * 2;
      Block *old = block;
      Block *nb = Block::allocate(new_cap);
      T *dst = nb->get_data();
      T *src = _data;
      new (&dst[0]) T(val);
      for (usz i = 0; i < _length; ++i)
        new (&dst[i + 1]) T(Xi::Move(src[i]));
      nb->_length = _length + 1;
      new (&dst[nb->_length]) T();
      Block::destroy(old);
      block = nb;
      _data = nb->get_data();
      _length = nb->_length;
    }
  }

  T shift() {
    if (_length == 0)
      return T();
    if (block->useCount > 1) {
      usz old_s = _length;
      T ret = _data[0];
      Block *nb = Block::allocate(old_s - 1);
      T *dst = nb->get_data();
      for (usz i = 1; i < old_s; ++i)
        new (&dst[i - 1]) T(_data[i]);
      nb->_length = old_s - 1;
      new (&dst[nb->_length]) T();
      destroy();
      block = nb;
      _data = nb->get_data();
      _length = nb->_length;
      return ret;
    }
    T ret = Xi::Move(_data[0]);
    _data[0].~T();
    new (&_data[0]) T(); // Additon
    _data++;
    _length--;
    offset++;
    return ret;
  }

  InlineArray begin(usz start) const {
    if (start >= _length)
      return InlineArray();
    InlineArray sub;
    sub.block = block;
    if (block)
      block->useCount++;
    sub._data = _data + start;
    sub._length = _length - start;
    sub.offset = offset + start;
    return sub;
  }

  InlineArray begin() const { return begin(0); }
  InlineArray end() const { return InlineArray(); }

  InlineArray begin(usz start, usz end) const {
    if (start >= _length)
      return InlineArray();
    usz avail = _length - start;
    usz len = (end - start);
    if (len > avail)
      len = avail;
    InlineArray copy;
    copy.allocate(len);
    for (usz i = 0; i < len; ++i)
      copy._data[i] = _data[start + i];
    copy.offset = offset + start;
    return copy;
  }

  template <int Rank> struct ViewProxy {
    T *data_ptr;
    const usz *strides_ptr;
    template <int R = Rank, typename Xi::EnableIf<(R > 1), int>::Type = 0>
    ViewProxy<Rank - 1> operator[](usz i) const {
      return ViewProxy<Rank - 1>{data_ptr + i * strides_ptr[0],
                                 strides_ptr + 1};
    }
    template <int R = Rank, typename Xi::EnableIf<(R == 1), int>::Type = 0>
    T &operator[](usz i) const {
      return data_ptr[i * strides_ptr[0]];
    }
  };

  template <int Rank> struct ViewContainer {
    InlineArray<T> *arr;
    usz dims[Rank];
    usz strides[Rank];
    template <typename S = String> S serialize() const {
      S s;
      s.pushVarLong(0);
      s.pushVarLong((long long)Rank);
      for (int i = 0; i < Rank; ++i)
        s.pushVarLong((long long)dims[i]);
      s.pushVarLong((long long)arr->offset);
      for (usz i = 0; i < arr->size(); ++i)
        s += Xi::serialize<T>((*arr)[i]);
      return s;
    }
    auto operator[](usz i) -> decltype(arr->operator[](i * strides[0])) {
      if constexpr (Rank > 1)
        return ViewProxy<Rank - 1>{arr->data() + i * strides[0], strides + 1};
      else
        return (*arr)[i * strides[0]];
    }
  };

  template <typename... Args>
  auto view(Args... args) -> ViewContainer<sizeof...(Args)> {
    constexpr int Rank = sizeof...(Args);
    ViewContainer<Rank> v;
    v.arr = this;
    usz d[] = {(usz)args...};
    usz current = 1;
    for (int i = Rank - 1; i >= 0; --i) {
      v.dims[i] = d[i];
      v.strides[i] = current;
      current *= d[i];
    }
    return v;
  }

  template <typename S = String> S serialize() const {
    S s;
    if (offset == 0 && _rank <= 1) {
      s.pushVarLong((long long)size() + 1);
    } else {
      s.pushVarLong(0);
      s.pushVarLong((long long)_rank);
      for (u8 i = 0; i < _rank; ++i)
        s.pushVarLong((long long)(_dims ? _dims[i] : size()));
      s.pushVarLong((long long)offset);
    }
    for (usz i = 0; i < size(); ++i)
      s += Xi::serialize<T>((*this)[i]);
    return s;
  }

  template <typename S = String> static InlineArray<T> deserialize(const S &s) {
    usz at = 0;
    return deserialize(s, at);
  }

  template <typename S = String>
  static InlineArray<T> deserialize(const S &s, usz &at) {
    InlineArray<T> res;
    auto headerRes = s.peekVarLong(at);
    if (headerRes.error)
      return res;
    at += headerRes.bytes;
    if (headerRes.value > 0) {
      usz size = (usz)(headerRes.value - 1);
      res.allocate(size);
      for (usz i = 0; i < size; ++i)
        res[i] = Xi::deserialize<T, S>(s, at);
    } else {
      auto rankRes = s.peekVarLong(at);
      if (rankRes.error)
        return res;
      at += rankRes.bytes;
      u8 rank = (u8)rankRes.value;
      res._rank = rank;
      res._dims = new usz[rank];
      usz total = 1;
      for (u8 i = 0; i < rank; i++) {
        auto d = s.peekVarLong(at);
        at += d.bytes;
        res._dims[i] = (usz)d.value;
        total *= (usz)d.value;
      }
      auto offsetRes = s.peekVarLong(at);
      if (offsetRes.error)
        return res;
      at += offsetRes.bytes;
      res.allocate(total);
      res.offset = (usz)offsetRes.value;
      for (usz i = 0; i < total; ++i)
        res[i] = Xi::deserialize<T, S>(s, at);
    }
    return res;
  }

  /**
   * @brief Removes a range of elements and collapses the gap.
   */
  void splice(usz start, usz length) {
    if (length == 0 || start >= _length)
      return;
    if (start + length > _length)
      length = _length - start;

    // Trigger CoW if shared
    if (block && block->useCount > 1) {
      allocate(_length);
    }

    T *ptr = _data;
    for (usz i = start + length; i < _length; ++i) {
      ptr[i - length] = Xi::Move(ptr[i]);
    }
    for (usz i = _length - length; i < _length; ++i) {
      ptr[i].~T();
    }
    _length -= length;
    if (block)
      block->_length = _length;
    new (&ptr[_length]) T(); // Ensure null terminator for strings
  }

  IMemoryDevice *getDevice() const { return block ? block->device : nullptr; }

  void *deviceView(i32 type = 0) const {
    if (!block || !block->device)
      return nullptr;
    return block->device->view(block->deviceHandle, type);
  }

  void wrapDevice(IMemoryDevice *dev, void *handle, usz count) {
    destroy();
    block = Block::wrapDevice(dev, handle, count);
    _data = nullptr;
    _length = count;
    offset = 0;
  }

  InlineArray<T> to() const {
    if (!block)
      return InlineArray<T>();
    if (!block->device) {
      InlineArray<T> res;
      res.allocate(size());
      if (_length > 0)
        memcpy(res.data(), data(), _length * sizeof(T));
      res.offset = offset;
      res._rank = _rank;
      if (_dims) {
        res._dims = new usz[_rank];
        for (u8 i = 0; i < _rank; i++)
          res._dims[i] = _dims[i];
      }
      return res;
    }
    InlineArray<T> cpu;
    usz count = block->_length;
    if (count == 0)
      return cpu;
    cpu.allocate(count);
    block->device->download(block->deviceHandle, cpu.data(), count * sizeof(T));
    cpu.offset = offset;
    cpu._rank = _rank;
    if (_dims) {
      cpu._dims = new usz[_rank];
      for (u8 i = 0; i < _rank; i++)
        cpu._dims[i] = _dims[i];
    }
    return cpu;
  }

  InlineArray<T> to(IMemoryDevice *dev) const {
    if (!dev)
      return to();
    InlineArray<T> src = to();
    usz count = src.size();
    if (count == 0)
      return InlineArray<T>();
    usz byteSize = count * sizeof(T);
    InlineArray<T> result;
    result.destroy();
    result.block = Block::allocateDevice(dev, byteSize);
    result._data = nullptr;
    result._length = count;
    result.offset = src.offset;
    result._rank = src._rank;
    if (src._dims) {
      result._dims = new usz[src._rank];
      for (u8 i = 0; i < src._rank; i++)
        result._dims[i] = src._dims[i];
    }
    dev->upload(result.block->deviceHandle, src.data(), byteSize);
    return result;
  }
};

#define XI_INLINE_ARRAY_BIN_OP(op)                                             \
  template <typename T>                                                        \
  InlineArray<T> operator op(const InlineArray<T> &a,                          \
                             const InlineArray<T> &b) {                        \
    usz n = a.size() < b.size() ? a.size() : b.size();                         \
    InlineArray<T> res;                                                        \
    res.allocate(n);                                                           \
    const T *d_a = a.data();                                                   \
    const T *d_b = b.data();                                                   \
    T *d_r = res.data();                                                       \
    for (usz i = 0; i < n; ++i)                                                \
      d_r[i] = d_a[i] op d_b[i];                                               \
    return res;                                                                \
  }                                                                            \
  template <typename T>                                                        \
  InlineArray<T> operator op(const InlineArray<T> &a, const T &b) {            \
    usz n = a.size();                                                          \
    InlineArray<T> res;                                                        \
    res.allocate(n);                                                           \
    const T *d_a = a.data();                                                   \
    T *d_r = res.data();                                                       \
    for (usz i = 0; i < n; ++i)                                                \
      d_r[i] = d_a[i] op b;                                                    \
    return res;                                                                \
  }                                                                            \
  template <typename T>                                                        \
  InlineArray<T> operator op(const T &a, const InlineArray<T> &b) {            \
    usz n = b.size();                                                          \
    InlineArray<T> res;                                                        \
    res.allocate(n);                                                           \
    const T *d_b = b.data();                                                   \
    T *d_r = res.data();                                                       \
    for (usz i = 0; i < n; ++i)                                                \
      d_r[i] = a op d_b[i];                                                    \
    return res;                                                                \
  }

XI_INLINE_ARRAY_BIN_OP(+)
XI_INLINE_ARRAY_BIN_OP(-)
XI_INLINE_ARRAY_BIN_OP(*)
XI_INLINE_ARRAY_BIN_OP(/)

} // namespace Collection

#endif // XI_CORE_INLINE_ARRAY_HPP
