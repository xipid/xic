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
template <typename T> class Array {
public:
  InlineArray<InlineArray<T>> fragments;

  Array() {}

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

  void remove(usz index) { splice(index, 1); }

  void clear() { fragments = InlineArray<InlineArray<T>>(); }

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