#ifndef XI_INLINE_ARRAY_HPP
#define XI_INLINE_ARRAY_HPP

#include "Primitives.hpp"

#if defined(AVR) || defined(ARDUINO)
#define XI_ARRAY_MIN_CAP 4
#else
#define XI_ARRAY_MIN_CAP 16
#endif

namespace Xi {

// Custom EnableIf to avoid <type_traits>
template <bool B, typename T = void> struct EnableIf {};
template <typename T> struct EnableIf<true, T> {
  using Type = T;
};

/**
 * @brief A high-performance, ref-counted, contiguous dynamic array.
 *
 * Behaves like std::vector but supports Copy-on-Write (CoW) slicing and
 * manual allocation control. Designed for use as fragments in sparse Arrays.
 *
 * @tparam T Element type.
 */
template <typename T> class InlineArray {
public:
  // -------------------------------------------------------------------------
  // Control Block
  // -------------------------------------------------------------------------
  struct Block {
    usz useCount;
    usz capacity;
    usz _length; // Used _length of the allocated memory (not necessarily the
                 // slice)

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
      return b;
    }

    static void destroy(Block *b) {
      if (!b)
        return;
      T *ptr = b->get_data();
      for (usz i = 0; i < b->_length; ++i) {
        ptr[i].~T();
      }
      ::operator delete(b);
    }
  };

  Block *block;
  T *_data;    ///< Pointer to start of this slice
  usz _length; ///< Length of this slice (renamed from _length for compat)
  usz offset;  ///< Global start index of this array

  // -------------------------------------------------------------------------
  // Constructors / Destructor
  // -------------------------------------------------------------------------

  /**
   * @brief Default constructor. Creates an empty array.
   */
  InlineArray() : block(nullptr), _data(nullptr), _length(0), offset(0) {}

  ~InlineArray() { destroy(); }

  InlineArray(const InlineArray &other)
      : block(other.block), _data(other._data), _length(other._length),
        offset(other.offset) {
    retain();
  }

  InlineArray(InlineArray &&other) noexcept
      : block(other.block), _data(other._data), _length(other._length),
        offset(other.offset) {
    other.block = nullptr;
    other._data = nullptr;
    other._length = 0;
    other.offset = 0;
  }

  InlineArray &operator=(const InlineArray &other) {
    if (this == &other)
      return *this;
    destroy();
    block = other.block;
    _data = other._data;
    _length = other._length;
    offset = other.offset;
    retain();
    return *this;
  }

  InlineArray &operator=(InlineArray &&other) noexcept {
    if (this == &other)
      return *this;
    destroy();
    block = other.block;
    _data = other._data;
    _length = other._length;
    offset = other.offset;
    other.block = nullptr;
    other._data = nullptr;
    other._length = 0;
    other.offset = 0;
    return *this;
  }

  /**
   * @brief Increments the reference count of the underlying block.
   */
  void retain() {
    if (block)
      block->useCount++;
  }

  /**
   * @brief Decrements reference count and destroys block if zero.
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

  // -------------------------------------------------------------------------
  // Memory Management
  // -------------------------------------------------------------------------

  /**
   * @brief Allocates memory for the array.
   *
   * @param len Number of elements to allocate.
   * @param safe If true, fails if reallocation is needed on shared memory.
   * @return true if allocation succeeded, false if safe check failed.
   */
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
      // Resize in place
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

    // Allocate new
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

  /**
   * @brief Ensures capacity for at least cap elements.
   * @param cap Minimal capacity required.
   * @return true if successful.
   */
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

  // -------------------------------------------------------------------------
  // Accessors
  // -------------------------------------------------------------------------

  /**
   * @brief Returns pointer to the underlying data.
   */
  T *data() { return _data; }

  /**
   * @brief Returns const pointer to the underlying data.
   */
  const T *data() const { return _data; }

  /**
   * @brief Returns the size of the array (or slice).
   */
  usz size() const { return _length; }

  /**
   * @brief Synonym for size() for JavaScript-like parity.
   */
  usz length_js() const { return _length; }

  /**
   * @brief Access element at global index.
   * @param idx Global index.
   * @return Reference to element.
   */
  T &operator[](usz idx) { return _data[idx]; }
  const T &operator[](usz idx) const { return _data[idx]; }

  /**
   * @brief Check if global index is within bounds.
   */
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

  // -------------------------------------------------------------------------
  // Mutators
  // -------------------------------------------------------------------------

  /**
   * @brief Append element to the end.
   */
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
    _data++;
    _length--;
    offset++;
    return ret;
  }

  // -------------------------------------------------------------------------
  // Views
  // -------------------------------------------------------------------------

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

  // ... (ViewProxy and ViewContainer omitted for brevity as they weren't
  // changed, but I'll keep the ones from step 85)
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
    usz strides[Rank];

    auto operator[](usz i)
        -> decltype(arr->operator[](i * strides[0])) { // Simplified
      if constexpr (Rank > 1) {
        return ViewProxy<Rank - 1>{arr->data() + i * strides[0], strides + 1};
      } else {
        return (*arr)[i * strides[0]];
      }
    }
  };

  template <typename... Args>
  auto view(Args... args) -> ViewContainer<sizeof...(Args)> {
    constexpr int Rank = sizeof...(Args);
    ViewContainer<Rank> v;
    v.arr = this;
    usz dims[] = {(usz)args...};
    usz current = 1;
    for (int i = Rank - 1; i >= 0; --i) {
      v.strides[i] = current;
      current *= dims[i];
    }
    return v;
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

} // namespace Xi

#endif // XI_INLINE_ARRAY_HPP
