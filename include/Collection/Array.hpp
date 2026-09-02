/**
 * @file Array.hpp
 * @brief Sparse and multidimensional array implementation for the Xi framework.

 */

#ifndef XI_CORE_ARRAY_HPP
#define XI_CORE_ARRAY_HPP

#include "InlineArray.hpp"

namespace Collection {

/**
 * @class Array
 * @brief A sparse array implementation using InlineArray fragments.
 *
 * This class manages a collection of potentially non-contiguous memory
 * fragments. It supports multidimensional views, sparse storage, and seamless
 * transfer between CPU and GPU devices.
 *
 * @tparam T The type of elements stored in the array.
 */
template <typename T> class XI_EXPORT Array {
public:
  mutable InlineArray<InlineArray<T>>
      fragments;        ///< Collection of memory fragments.
  usz *_dims = nullptr; ///< Multidimensional shape dimensions.
  u8 _rank = 1;         ///< Rank (number of dimensions).

  /**
   * @brief Default constructor (1D array).
   */
  Array() : _dims(nullptr), _rank(1) {}

  /**
   * @brief Destructor.
   */
  ~Array() {
    if (_dims) {
      delete[] _dims;
      _dims = nullptr;
    }
  }

  /**
   * @brief Copy constructor (deep copy of metadata).
   */
  Array(const Array &other)
      : _dims(nullptr), _rank(other._rank) {
    if (other._dims) {
      _dims = new usz[_rank];
      for (u8 i = 0; i < _rank; i++)
        _dims[i] = other._dims[i];
    }
    for (usz i = 0; i < other.fragments.size(); ++i) {
      InlineArray<T> f;
      f.offset = other.fragments[i].offset;
      for (usz k = 0; k < other.fragments[i].size(); ++k) {
        f.push(other.fragments[i][k]);
      }
      fragments.push(Xi::Move(f));
    }
  }

  /**
   * @brief Move constructor.
   */
  Array(Array &&other) noexcept
      : fragments(Xi::Move(other.fragments)), _dims(other._dims),
        _rank(other._rank) {
    other._dims = nullptr;
    other._rank = 1;
  }


  /**
   * @brief Copy assignment operator.
   */
  Array &operator=(const Array &other) {
    if (this == &other)
      return *this;
    fragments.destroy();
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
    for (usz i = 0; i < other.fragments.size(); ++i) {
      InlineArray<T> f;
      f.offset = other.fragments[i].offset;
      for (usz k = 0; k < other.fragments[i].size(); ++k) {
        f.push(other.fragments[i][k]);
      }
      fragments.push(Xi::Move(f));
    }
    return *this;
  }

  /**
   * @brief Move assignment operator.
   */
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

  /**
   * @brief Sets the multidimensional shape of the array.
   */
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

  /**
   * @brief Returns the rank (number of dimensions) of the array.
   */
  u8 rank() const { return _rank; }

  /**
   * @brief Returns the dimension shape array pointer.
   */
  const usz *dimensions() const { return _dims; }

  /**
   * @brief Resizes the array to fit a specified total length.
   * @param len Target total length across all fragments.
   * @return true if successful.
   */
  bool allocate(usz len) {
    if (fragments.size() == 0) {
      if (len == 0)
        return true;
      InlineArray<T> chunk;
      if (!chunk.allocate(len))
        return false;
      chunk.offset = 0;
      fragments.push(Xi::Move(chunk));
      return true;
    }

    while (fragments.size() > 0) {
      InlineArray<T> &last = fragments.data()[fragments.size() - 1];
      usz start = last.offset;

      if (len > start) {
        usz new_local_len = len - start;
        return last.allocate(new_local_len);
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
   * @brief Replaces contents with data from a raw pointer.
   */
  void set(const T *vals, usz count) {
    clear();
    if (vals && count > 0) {
      for (usz i = 0; i < count; ++i) {
        push(vals[i]);
      }
    }
  }

  /**
   * @brief Pre-allocates capacity in the last fragment.
   */
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
   * @brief Flattens all sparse fragments into a single contiguous block of
   * memory.
   * @return Pointer to contiguous data.
   */
  T *data() {
    if (fragments.size() == 0)
      return nullptr;
    if (fragments.size() == 1 && fragments.data()[0].offset == 0)
      return fragments.data()[0].data();

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
      for (usz k = 0; k < count; ++k)
        dst[start + k] = src[k];
    }

    InlineArray<InlineArray<T>> new_frags;
    flat.offset = 0;
    new_frags.push(Xi::Move(flat));
    fragments = Xi::Move(new_frags);

    return fragments.data()[0].data();
  }

  const T *data() const { return const_cast<Array<T> *>(this)->data(); }

  /**
   * @brief Forces a fragment boundary at the specified index.
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
   * @brief Removes a range of elements and collapses the gap.
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
        if (f_start < start) {
          InlineArray<T> p1 = f.begin(0, start - f_start);
          new_frags.push(Xi::Move(p1));
        }
        if (f_end > end) {
          InlineArray<T> p2 = f.begin(end - f_start);
          p2.offset = start;
          new_frags.push(Xi::Move(p2));
        }
      }
    }
    fragments = Xi::Move(new_frags);
  }

  /**
   * @brief Array subscript operator with automatic sparse allocation.
   */
  T &operator[](usz i) {
    long long best_ext = -1, best_pre = -1;

    for (usz k = 0; k < fragments.size(); ++k) {
      InlineArray<T> &f = fragments[k];
      if (f.has(i))
        return f[i - f.offset];
      if (i == f.offset + f.size())
        best_ext = k;
      if (f.offset > 0 && i == f.offset - 1)
        best_pre = k;
    }

    if (best_ext != -1) {
      fragments[best_ext].push(T());
      return fragments[best_ext][i - fragments[best_ext].offset];
    }

    if (best_pre != -1) {
      fragments[best_pre].unshift(T());
      fragments[best_pre].offset--;
      return fragments[best_pre][0];
    }

    InlineArray<T> chunk;
    chunk.allocate(1);
    chunk.offset = i;

    usz pos = 0;
    while (pos < fragments.size() && fragments[pos].offset < i)
      pos++;

    fragments.push(InlineArray<T>());
    for (long long k = fragments.size() - 1; k > (long long)pos; --k) {
      fragments[(usz)k] = Xi::Move(fragments[(usz)k - 1]);
    }
    fragments[pos] = Xi::Move(chunk);
    return fragments[pos][0];
  }

  const T &operator[](usz i) const {
    for (usz k = 0; k < fragments.size(); ++k) {
      if (fragments[k].has(i))
        return fragments[k][i - fragments[k].offset];
    }
    static T dummy;
    return dummy;
  }

  usz size() const {
    if (fragments.size() == 0 || !fragments.data())
      return 0;
    const InlineArray<T> &last = fragments.data()[fragments.size() - 1];
    return last.offset + last.size();
  }

  usz length() const { return size(); }

  /**
   * @brief Appends an element to the end of the array.
   */
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

  /**
   * @brief Removes and returns the first element.
   */
  T shift() {
    if (fragments.size() == 0)
      return T();
    InlineArray<T> &f = fragments.data()[0];
    T val = f.shift();
    if (f.size() == 0)
      fragments.shift();
    for (usz i = 0; i < fragments.size(); ++i)
      if (fragments.data()[i].offset > 0)
        fragments.data()[i].offset--;
    return val;
  }

  /**
   * @brief Prepends an element to the beginning of the array.
   */
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
    for (usz i = 1; i < fragments.size(); ++i)
      fragments.data()[i].offset++;
  }

  /**
   * @brief Removes and returns the last element.
   */
  T pop() {
    if (fragments.size() == 0)
      return T();
    InlineArray<T> &last = fragments.data()[fragments.size() - 1];
    T val = last.pop();
    if (last.size() == 0)
      fragments.pop();
    return val;
  }

  /**
   * @brief Finds the first occurrence of a value.
   */
  long long find(const T &val) const {
    for (usz i = 0; i < fragments.size(); ++i) {
      long long idx = fragments.data()[i].indexOf(val);
      if (idx != -1)
        return idx;
    }
    return -1;
  }

  /**
   * @brief Clears all fragments.
   */
  void clear() { fragments = InlineArray<InlineArray<T>>(); }

  // --- Serialization ---

  /**
   * @brief Serializes the array (sparse-aware).
   */
  template <typename S = String> S serialize() const {
    S s;
    if (_rank <= 1) {
      s.pushVarLong((long long)size() + 1);
    } else {
      s.pushVarLong(0);
      s.pushVarLong((long long)_rank);
      for (u8 i = 0; i < _rank; ++i)
        s.pushVarLong((long long)(_dims ? _dims[i] : size()));
      s.pushVarLong(0);
    }
    for (usz i = 0; i < size(); ++i)
      s += Xi::serialize<T>((*this)[i]);
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
      for (usz i = 0; i < total; ++i)
        res[i] = Xi::deserialize<T, S>(s, at);
    }
    return res;
  }

  // --- Iterators ---

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

  // --- Multidimensional Views ---

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
      if constexpr (Rank > 1)
        return ViewProxy<Rank - 1>{arr, strides + 1, i * strides[0]};
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

  // --- Device Transfer ---

  void *deviceView(i32 type = 0) const {
    if (fragments.size() != 1)
      return nullptr;
    return fragments[0].deviceView(type);
  }

  /**
   * @brief Clones the array to CPU.
   */
  Array<T> to() const {
    Array<T> res;
    for (usz i = 0; i < fragments.size(); ++i)
      res.fragments.push(fragments.data()[i].to());
    if (_dims) {
      res._dims = new usz[_rank];
      for (u8 i = 0; i < _rank; i++)
        res._dims[i] = _dims[i];
    }
    res._rank = _rank;
    return res;
  }

  /**
   * @brief Transfers the array to a specific memory device.
   */
  Array<T> to(IMemoryDevice *dev) const {
    if (!dev)
      return to();
    Array<T> res;
    for (usz i = 0; i < fragments.size(); ++i)
      res.fragments.push(fragments.data()[i].to(dev));
    if (_dims) {
      res._dims = new usz[_rank];
      for (u8 i = 0; i < _rank; i++)
        res._dims[i] = _dims[i];
    }
    res._rank = _rank;
    return res;
  }

  bool includes(const T& val) const {
      for (usz i = 0; i < size(); ++i) {
          if (((Array<T>*)this)->operator[](i) == val) return true;
      }
      return false;
  }

  Array<T> intersect(const Array<T>& o) const {
      Array<T> res;
      for (usz i = 0; i < size(); ++i) {
          T val = ((Array<T>*)this)->operator[](i);
          if (o.includes(val) && !res.includes(val)) {
              res.push(val);
          }
      }
      return res;
  }

  Array<T> uni(const Array<T>& o) const {
      Array<T> res;
      for (usz i = 0; i < size(); ++i) {
          T val = ((Array<T>*)this)->operator[](i);
          if (!res.includes(val)) res.push(val);
      }
      for (usz i = 0; i < o.size(); ++i) {
          T val = ((Array<T>&)o)[i];
          if (!res.includes(val)) res.push(val);
      }
      return res;
  }

  Array<T> difference(const Array<T>& o) const {
      Array<T> res;
      for (usz i = 0; i < size(); ++i) {
          T val = ((Array<T>*)this)->operator[](i);
          if (!o.includes(val) && !res.includes(val)) {
              res.push(val);
          }
      }
      return res;
  }

  Array<T> operator&(const Array<T>& o) const { return intersect(o); }
  Array<T> operator|(const Array<T>& o) const { return uni(o); }
  Array<T> operator-(const Array<T>& o) const { return difference(o); }
};

// -------------------------------------------------------------------------
// Generic Element-wise Operations
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

} // namespace Collection

#endif // XI_CORE_ARRAY_HPP