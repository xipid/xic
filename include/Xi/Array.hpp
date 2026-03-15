#ifndef XI_ARRAY_HPP
#define XI_ARRAY_HPP

#include "InlineArray.hpp"

namespace Xi {

/**
 * @brief A sparse array implementation using InlineArray fragments.
 *
 * Manages a collection of InlineArray fragments to support sparse data
 * storage efficiently.
 *
 * @tparam T Element type.
 */
template <typename T> class XI_EXPORT Array {
public:
  InlineArray<InlineArray<T>> fragments;
  usz *_dims;
  u8 _rank;

  Array() : _dims(nullptr), _rank(1) {}

  ~Array() {
    if (_dims) {
      delete[] _dims;
      _dims = nullptr;
    }
  }

  Array(const Array &other)
      : fragments(other.fragments), _dims(nullptr), _rank(other._rank) {
    if (other._dims) {
      _dims = new usz[_rank];
      for (u8 i = 0; i < _rank; i++)
        _dims[i] = other._dims[i];
    }
  }

  Array(Array &&other) noexcept
      : fragments(Xi::Move(other.fragments)), _dims(other._dims),
        _rank(other._rank) {
    other._dims = nullptr;
    other._rank = 1;
  }

  Array &operator=(const Array &other) {
    if (this == &other)
      return *this;
    fragments = other.fragments;
    if (_dims) {
      delete[] _dims;
      _dims = nullptr;
    }
    _rank = other._rank;
    if (other._dims) {
      _dims = new usz[_rank];
      for (u8 i = 0; i < _rank; i++)
        _dims[i] = other._dims[i];
    }
    return *this;
  }

  Array &operator=(Array &&other) noexcept {
    if (this == &other)
      return *this;
    fragments = Xi::Move(other.fragments);
    if (_dims) {
      delete[] _dims;
      _dims = nullptr;
    }
    _dims = other._dims;
    _rank = other._rank;
    other._dims = nullptr;
    other._rank = 1;
    return *this;
  }

  Array &shape(u8 rank, const usz *d) {
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

  // -------------------------------------------------------------------------
  // Management
  // -------------------------------------------------------------------------

  /**
   * @brief Resize the array (specifically the last fragment) to support length.
   * @param len New total length required.
   * @return true if successful.
   */
  bool allocate(usz len) {
    if (fragments.size() == 0) {
      if (len == 0)
        return true;
      InlineArray<T> chunk;
      if (!chunk.allocate(len))
        return false;
      chunk.offset = 0; // First chunk starts at 0
      fragments.push(Xi::Move(chunk));
      return true;
    }

    while (fragments.size() > 0) {
      InlineArray<T> &last = fragments.data()[fragments.size() - 1];
      usz start = last.offset;

      if (len > start) {
        usz new_local_len = len - start;
        return last.allocate(new_local_len);
      } else if (len == start) {
        fragments.pop();
      } else {
        fragments.pop();
      }
    }

    if (len > 0 && fragments.size() == 0) {
      InlineArray<T> chunk;
      chunk.allocate(len);
      chunk.offset = 0;
      fragments.push(Xi::Move(chunk));
    }
    return true;
  }

  /**
   * @brief Clear existing data and replace with data from pointer.
   * @param vals Pointer to source data.
   * @param count Number of elements.
   */
  void set(const T *vals, usz count) {
    allocate(0); // Clear all fragments
    if (count > 0 && vals) {
      pushEach(vals, count);
    }
  }

  bool reserve(usz len) {
    if (fragments.size() == 0) {
      InlineArray<T> chunk;
      if (!chunk.reserve(len))
        return false;
      chunk.offset = 0;
      fragments.push(Xi::Move(chunk));
      return true;
    }
    return fragments.data()[fragments.size() - 1].reserve(len);
  }

  /**
   * @brief Flatten all fragments into a single contiguous array.
   * @return Pointer to the beginning of the contiguous data.
   */
  T *data() {
    if (fragments.size() == 0)
      return nullptr;
    if (fragments.size() == 1) {
      InlineArray<T> &f = fragments.data()[0];
      if (f.offset == 0)
        return f.data();
    }

    InlineArray<T> &last = fragments.data()[fragments.size() - 1];
    usz total_len = last.offset + last.size();

    InlineArray<T> flat;
    if (!flat.allocate(total_len))
      return nullptr;

    T *dst = flat.data();

    for (usz i = 0; i < fragments.size(); ++i) {
      InlineArray<T> &f = fragments.data()[i];
      const T *src = f.data();
      usz count = f.size();
      usz start = f.offset;
      for (usz k = 0; k < count; ++k) {
        dst[start + k] = src[k];
      }
    }

    InlineArray<InlineArray<T>> new_frags;
    flat.offset = 0;
    new_frags.push(Xi::Move(flat));
    fragments = Xi::Move(new_frags);

    return fragments.data()[0].data();
  }

  /**
   * @brief Split a fragment at the specified global index.
   * @param at Global index to split at.
   */
  void break_at(usz at) {
    for (long long i = 0; i < (long long)fragments.size(); ++i) {
      InlineArray<T> &f = fragments.data()[i];
      usz start = f.offset;
      usz end = start + f.size();

      if (at > start && at < end) {
        usz rel = at - start;
        InlineArray<T> suff = f.begin(rel);
        f.allocate(rel);

        fragments.push(InlineArray<T>());
        for (long long k = fragments.size() - 1; k > i + 1; --k) {
          fragments.data()[(usz)k] = Xi::Move(fragments.data()[(usz)k - 1]);
        }
        fragments.data()[i + 1] = Xi::Move(suff);
        return;
      }
    }
  }

  /**
   * @brief Remove a range of elements (shifting subsequent elements).
   * @param start Global start index.
   * @param length Number of elements to remove.
   */
  void splice(usz start, usz length) {
    if (length == 0)
      return;
    usz end = start + length;

    InlineArray<InlineArray<T>> new_frags;

    for (usz i = 0; i < fragments.size(); ++i) {
      InlineArray<T> &f = fragments.data()[i];
      usz f_start = f.offset;
      usz f_end = f_start + f.size();

      if (f_end <= start) {
        new_frags.push(Xi::Move(f));
      } else if (f_start >= end) {
        f.offset -= length;
        new_frags.push(Xi::Move(f));
      } else {
        // Overlap
        if (f_start < start) {
          usz keep_len = start - f_start;
          InlineArray<T> p1 = f.begin(0, keep_len);
          new_frags.push(Xi::Move(p1));
        }

        if (f_end > end) {
          usz skip = end - f_start;
          InlineArray<T> p2 =
              f.begin(skip); // offset becomes f.offset + skip = end
          p2.offset = start;
          new_frags.push(Xi::Move(p2));
        }
      }
    }
    fragments = Xi::Move(new_frags);
  }

  T &operator[](usz i) {
    long long best_ext = -1;
    long long best_pre = -1;

    for (usz k = 0; k < fragments.size(); ++k) {
      InlineArray<T> &f = fragments[k];
      if (f.has(i))
        return f[i - f.offset];

      usz end = f.offset + f.size();
      if (i == end)
        best_ext = k;
      if (f.offset > 0 && i == f.offset - 1)
        best_pre = k;
    }

    if (best_ext != -1) {
      InlineArray<T> &f = fragments[best_ext];
      f.push(T());
      return f[i - f.offset];
    }

    if (best_pre != -1) {
      InlineArray<T> &f = fragments[best_pre];
      f.unshift(T());
      f.offset--;
      return f[i - f.offset];
    }

    InlineArray<T> chunk;
    chunk.allocate(1);
    chunk.offset = i;

    // Insert Sorted
    usz pos = 0;
    while (pos < fragments.size() && fragments[pos].offset < i)
      pos++;

    {
      // Manual insertion into fragments because InlineArray doesn't have
      // insert()
      fragments.push(InlineArray<T>());
      for (long long k = fragments.size() - 1; k > (long long)pos; --k) {
        fragments[(usz)k] = Xi::Move(fragments[(usz)k - 1]);
      }
      fragments[pos] = Xi::Move(chunk);
    }
    return fragments[pos][0];
  }

  const T &operator[](usz i) const {
    for (usz k = 0; k < fragments.size(); ++k) {
      const InlineArray<T> &f = fragments[k];
      if (f.has(i))
        return f[i - f.offset];
    }
    static T dummy;
    return dummy;
  }

  usz size() const {
    if (fragments.size() == 0)
      return 0;
    const InlineArray<T> &last = fragments.data()[fragments.size() - 1];
    return last.offset + last.size();
  }

  usz length() const { return size(); }

  void push(const T &val) {
    if (fragments.size() > 0) {
      fragments.data()[fragments.size() - 1].push(val);
    } else {
      InlineArray<T> chunk;
      chunk.offset = 0;
      chunk.push(val);
      fragments.push(Xi::Move(chunk));
    }
  }

  T shift() {
    if (fragments.size() == 0)
      return T();
    InlineArray<T> &f = fragments.data()[0];
    T val = f.shift();
    if (f.size() == 0) {
      fragments.shift();
    }

    for (usz i = 0; i < fragments.size(); ++i) {
      if (fragments.data()[i].offset > 0)
        fragments.data()[i].offset--;
    }
    return val;
  }

  void unshift(const T &val) {
    if (fragments.size() == 0) {
      InlineArray<T> chunk;
      chunk.offset = 0;
      chunk.push(val);
      fragments.push(Xi::Move(chunk));
      return;
    }
    InlineArray<T> &f = fragments.data()[0];
    if (f.offset > 0) {
      f.unshift(val);
      f.offset--;
    } else {
      f.unshift(val);
    }
    for (usz i = 1; i < fragments.size(); ++i) {
      fragments.data()[i].offset++;
    }
  }

  T pop() {
    if (fragments.size() == 0)
      return T();
    InlineArray<T> &last = fragments.data()[fragments.size() - 1];
    T val = last.pop();
    if (last.size() == 0) {
      fragments.pop();
    }
    return val;
  }

  long long find(const T &val) const {
    for (usz i = 0; i < fragments.size(); ++i) {
      const InlineArray<T> &f = fragments.data()[i];
      long long idx = f.indexOf(val);
      if (idx != -1)
        return idx;
    }
    return -1;
  }

  void clear() { fragments = InlineArray<InlineArray<T>>(); }

  // --- Serialization ---
  /**
   * @brief Intelligently serializes the sparse array with dimension support.
   */
  template <typename S = String> S serialize() const {
    S s;
    if (_rank <= 1) {
      s.pushVarLong((long long)size() + 1);
    } else {
      s.pushVarLong(0); // Marker
      s.pushVarLong((long long)_rank);
      for (u8 i = 0; i < _rank; ++i) {
        s.pushVarLong((long long)(_dims ? _dims[i] : size()));
      }
      s.pushVarLong(0); // Offset (0 for standard Array)
    }
    for (usz i = 0; i < size(); ++i) {
      s += Xi::serialize<T>((*this)[i]);
    }
    return s;
  }

  template <typename S = String> static Array<T> deserialize(const S &s) {
    usz at = 0;
    return deserialize(s, at);
  }

  template <typename S = String>
  static Array<T> deserialize(const S &s, usz &at) {
    Array<T> res;
    auto headerRes = s.peekVarLong(at);
    if (headerRes.error)
      return res;
    at += headerRes.bytes;

    if (headerRes.value > 0) {
      usz size = (usz)(headerRes.value - 1);
      res.allocate(size);
      for (usz i = 0; i < size; ++i) {
        res[i] = Xi::deserialize<T, S>(s, at);
      }
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
      for (usz i = 0; i < total; ++i) {
        res[i] = Xi::deserialize<T, S>(s, at);
      }
    }
    return res;
  }

  // -------------------------------------------------------------------------
  // Iterators
  // -------------------------------------------------------------------------

  struct Iterator {
    Array<T> *arr;
    usz globalIdx;

    Iterator(Array<T> *a, usz idx) : arr(a), globalIdx(idx) {}

    bool operator!=(const Iterator &o) const {
      return globalIdx != o.globalIdx || arr != o.arr;
    }

    Iterator &operator++() {
      globalIdx++;
      return *this;
    }

    T &operator*() { return (*arr)[globalIdx]; }
  };

  struct ConstIterator {
    const Array<T> *arr;
    usz globalIdx;

    ConstIterator(const Array<T> *a, usz idx) : arr(a), globalIdx(idx) {}

    bool operator!=(const ConstIterator &o) const {
      return globalIdx != o.globalIdx || arr != o.arr;
    }

    ConstIterator &operator++() {
      globalIdx++;
      return *this;
    }

    const T &operator*() const { return (*arr)[globalIdx]; }
  };

  Iterator begin() { return Iterator(this, 0); }
  Iterator end() { return Iterator(this, size()); }

  ConstIterator begin() const { return ConstIterator(this, 0); }
  ConstIterator end() const { return ConstIterator(this, size()); }

  // -------------------------------------------------------------------------
  // Device Transfer
  // -------------------------------------------------------------------------

  /**
   * @brief Returns a multidimensional view of the array.
   */
  template <int Rank> struct ViewProxy {
    Array<T> *arr;
    const usz *strides;
    usz accumulated;

    template <int R = Rank, typename Xi::EnableIf<(R > 1), int>::Type = 0>
    ViewProxy<Rank - 1> operator[](usz i) const {
      return ViewProxy<Rank - 1>{arr, strides + 1,
                                 accumulated + i * strides[0]};
    }

    template <int R = Rank, typename Xi::EnableIf<(R == 1), int>::Type = 0>
    T &operator[](usz i) const {
      return (*arr)[accumulated + i * strides[0]];
    }
  };

  template <int Rank> struct ViewContainer {
    Array<T> *arr;
    usz dims[Rank];
    usz strides[Rank];

    auto operator[](usz i) {
      if constexpr (Rank > 1) {
        return ViewProxy<Rank - 1>{arr, strides + 1, i * strides[0]};
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
    usz d[] = {(usz)args...};
    usz current = 1;
    for (int i = Rank - 1; i >= 0; --i) {
      v.dims[i] = d[i];
      v.strides[i] = current;
      current *= d[i];
    }
    return v;
  }

  /**
   * @brief Get the device-specific view of this array's data.
   * Works if the array consists of a single fragment on a device.
   */
  void *deviceView(i32 type = 0) const {
    if (fragments.size() != 1)
      return nullptr;
    return fragments[0].deviceView(type);
  }

  /**
   * @brief Copy all fragments to CPU. Each fragment downloads individually.
   */
  Array<T> to() const {
    Array<T> res;
    for (usz i = 0; i < fragments.size(); ++i) {
      res.fragments.push(fragments.data()[i].to());
    }
    if (_dims) {
      res._dims = new usz[_rank];
      for (u8 i = 0; i < _rank; i++)
        res._dims[i] = _dims[i];
    }
    res._rank = _rank;
    return res;
  }

  /**
   * @brief Move each individual fragment to the target device.
   * Maintains fragmentation on the device.
   */
  Array<T> to(IMemoryDevice *dev) const {
    if (!dev)
      return to();

    Array<T> res;
    for (usz i = 0; i < fragments.size(); ++i) {
      res.fragments.push(fragments.data()[i].to(dev));
    }
    if (_dims) {
      res._dims = new usz[_rank];
      for (u8 i = 0; i < _rank; i++)
        res._dims[i] = _dims[i];
    }
    res._rank = _rank;
    return res;
  }
};

// -------------------------------------------------------------------------
// Operator Overloads
// -------------------------------------------------------------------------

#define XI_ARRAY_BIN_OP(op)                                                    \
  template <typename T>                                                        \
  Array<T> operator op(const Array<T> &a, const Array<T> &b) {                 \
    usz n = a.size() < b.size() ? a.size() : b.size();                         \
    Array<T> res;                                                              \
    res.allocate(n);                                                           \
    for (usz i = 0; i < n; ++i)                                                \
      res[i] = a[i] op b[i];                                                   \
    return res;                                                                \
  }                                                                            \
  template <typename T> Array<T> operator op(const Array<T> &a, const T &b) {  \
    usz n = a.size();                                                          \
    Array<T> res;                                                              \
    res.allocate(n);                                                           \
    for (usz i = 0; i < n; ++i)                                                \
      res[i] = a[i] op b;                                                      \
    return res;                                                                \
  }                                                                            \
  template <typename T> Array<T> operator op(const T &a, const Array<T> &b) {  \
    usz n = b.size();                                                          \
    Array<T> res;                                                              \
    res.allocate(n);                                                           \
    for (usz i = 0; i < n; ++i)                                                \
      res[i] = a op b[i];                                                      \
    return res;                                                                \
  }

XI_ARRAY_BIN_OP(+)
XI_ARRAY_BIN_OP(-)
XI_ARRAY_BIN_OP(*)
XI_ARRAY_BIN_OP(/)

} // namespace Xi

#endif // XI_ARRAY_HPP