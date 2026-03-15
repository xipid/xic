#ifndef STRING_HPP
#define STRING_HPP

#include "Array.hpp"
#include "InlineArray.hpp"
#include "Random.hpp"
namespace Xi {
// Type checking helpers
template <typename T> struct HasToString {
  template <typename U> static char test(decltype(&U::toString));
  template <typename U> static long test(...);
  static const bool value = sizeof(test<T>(0)) == sizeof(char);
};

class String;
class Regex;

// Serialization methods are now integrated into String and Map.

int parseInt(const String &s);
long long parseLong(const String &s);
f64 parseDouble(const String &s);
void secureRandomFill(u8 *buffer, usz size);

struct VarLongResult {
  long long value;
  int bytes;
  bool error;
};

/**
 * @brief A mutable string class inheriting from InlineArray<u8>.
 *
 * Provides string manipulation capabilities, including concatenation,
 * splitting, replacement, and numeric conversions. It is designed to be
 * compatible with Xi's networking and crypto utilities.
 *
 * Supports Copy-On-Write (COW) optimization via InlineArray.
 */
class XI_EXPORT String : public InlineArray<u8> {
private:
  static usz str_len(const char *s);
  void append_raw(const u8 *b, usz c);

  template <typename I> void append_int(I n) {
    if (n == 0) {
      push('0');
      return;
    }
    char buf[32];
    int i = 0;
    bool neg = (n < 0);
    unsigned long long un =
        neg ? (unsigned long long)(-(n + 1)) + 1 : (unsigned long long)n;
    while (un) {
      buf[i++] = (un % 10) + '0';
      un /= 10;
    }
    if (neg)
      buf[i++] = '-';
    while (i > 0)
      push(buf[--i]);
  }

  void append_f32(f64 n, int precision = 6);

public:
  using InlineArray<u8>::push;
  using InlineArray<u8>::shift;
  using InlineArray<u8>::unshift;
  using InlineArray<u8>::allocate;

  /**
   * @brief Concatenates another String to this one.
   */
  void concat(const String &other);

  String() : InlineArray<u8>() {}

  /**
   * @brief Construct from C-string.
   */
  String(const char *s);

  /**
   * @brief Construct from raw buffer.
   */
  String(const u8 *buffer, usz l);

  /**
   * @brief Replaces content with data from raw address.
   * Useful for JIT/Interop.
   */
  void setFromRawAddress(unsigned long long ptrAddr, usz len);

  ~String() {}

  void clear() { destroy(); }

  String(const String &o) : InlineArray<u8>(o) {}

  String(String &&o) noexcept : InlineArray<u8>(Xi::Move(o)) {}

  String &operator=(const String &o) {
    InlineArray<u8>::operator=(o);
    return *this;
  }

  String &operator=(String &&o) noexcept {
    InlineArray<u8>::operator=(Xi::Move(o));
    return *this;
  }

  String(const InlineArray<u8> &o) : InlineArray<u8>(o) {}

  String(int n);
  String(long long n);
  String(u64 n);
  String(f64 n);
  String(f32 n);

  /**
   * @brief Returns C-string representation.
   * Modifies the string to ensure null-termination if needed, then returns
   * pointer.
   */
  const char *c_str();
  const char *c_str() const { return const_cast<String *>(this)->c_str(); }
  explicit operator const char *() const {
    return const_cast<String *>(this)->c_str();
  }

  // --- JavaScript-like / Buffer-like API ---

  usz length() const { return size(); }
  bool isEmpty() const { return size() == 0; }

  static String from(const char *s) { return String(s); }
  static String from(const u8 *buf, usz l) { return String(buf, l); }
  static String from(const String &s) { return String(s); }

  void fill(u8 val);

  long long indexOf(const char *needle, usz start = 0) const;

  long long indexOf(char needle, usz start = 0) const;

  bool includes(const char *needle, usz start = 0) const;

  bool startsWith(const char *prefix) const;

  bool endsWith(const char *suffix) const;

  String slice(long long start, long long end = -1) const;

  String substring(usz start, usz end = (usz)-1) const;

  String trim() const;

  String toUpperCase() const;

  String toLowerCase() const;

  char charAt(usz idx) const;

  int charCodeAt(usz idx) const;

  String padStart(usz targetLen, char padChar = ' ') const;

  String padEnd(usz targetLen, char padChar = ' ') const;

  static String *create() { return new String(); }

  String &operator+=(const char *s);
  String &operator+=(const String &o);
  String &operator+=(char c);
  String &operator+=(int n);
  String &operator+=(long long n);
  template <typename T>
  auto operator+=(const T &obj) -> decltype(obj.toString(), *this) {
    *this += obj.toString();
    return *this;
  }

  bool operator==(const String &other) const;
  bool operator!=(const String &other) const { return !(*this == other); }

  bool operator==(const char *other) const;
  bool operator!=(const char *other) const { return !(*this == other); }

  friend String operator+(const String &lhs, const String &rhs) {
    String s = lhs;
    s += rhs;
    return s;
  }
  friend String operator+(const String &lhs, const char *rhs) {
    String s = lhs;
    s += rhs;
    return s;
  }
  friend String operator+(const char *lhs, const String &rhs) {
    String s(lhs);
    s += rhs;
    return s;
  }

  long long find(const char *needle, usz start = 0) const;

  // Shadow InlineArray::begin to return String instead of InlineArray
  String begin(usz from, usz to) const {
    return static_cast<String>(
        const_cast<String *>(this)->InlineArray<u8>::begin(from, to));
  }
  String begin() const {
    return static_cast<String>(
        const_cast<String *>(this)->InlineArray<u8>::begin());
  }

  Array<String> split(const Regex &reg) const;
  Array<String> split(const char *sep) const;

  String replace(const Regex &reg, const String &rep) const;
  String replace(const char *tgt, const char *rep) const;

  int toInt() const { return Xi::parseInt(*this); }
  f64 toDouble() const { return Xi::parseDouble(*this); }

  bool constantTimeEquals(const Xi::String &b, int length = 0) const;

  String *pushVarLong(long long v);

  long long shiftVarLong();

  String *unshiftVarLong(long long v);
  String *unshiftVarLong() { return unshiftVarLong((long long)size()); }

  void pushVarString(const String &s);

  String shiftVarString();

  String *pushBool(bool v);
  bool shiftBool();

  String *pushI64(long long v);
  long long shiftI64();

  String *pushF64(f64 v);
  f64 shiftF64();

  String *pushF32(f32 v);
  f32 shiftF32();

  VarLongResult peekVarLong(usz offset = 0) const;

  Xi::String toDeci() const;

  static void check_abi() {}
};

// -------------------------------------------------------------------------
// Serialization Implementation
// -------------------------------------------------------------------------

template <typename T> struct Arity {
  struct Any {
    template <typename U> operator U();
  };
  template <typename U, typename... Args>
  static auto test(int) -> decltype(U{Xi::DeclVal<Args>()...}, char());
  template <typename U, typename... Args> static long test(...);

  static const usz Value =
      (sizeof(test<T, Any, Any, Any, Any, Any, Any, Any, Any, Any, Any, Any,
                   Any, Any, Any, Any, Any>(0)) == sizeof(char))
          ? 16
      : (sizeof(test<T, Any, Any, Any, Any, Any, Any, Any, Any, Any, Any, Any,
                     Any, Any, Any, Any>(0)) == sizeof(char))
          ? 15
      : (sizeof(test<T, Any, Any, Any, Any, Any, Any, Any, Any, Any, Any, Any,
                     Any, Any, Any>(0)) == sizeof(char))
          ? 14
      : (sizeof(test<T, Any, Any, Any, Any, Any, Any, Any, Any, Any, Any, Any,
                     Any, Any>(0)) == sizeof(char))
          ? 13
      : (sizeof(test<T, Any, Any, Any, Any, Any, Any, Any, Any, Any, Any, Any,
                     Any>(0)) == sizeof(char))
          ? 12
      : (sizeof(test<T, Any, Any, Any, Any, Any, Any, Any, Any, Any, Any, Any>(
             0)) == sizeof(char))
          ? 11
      : (sizeof(test<T, Any, Any, Any, Any, Any, Any, Any, Any, Any, Any>(0)) ==
         sizeof(char))
          ? 10
      : (sizeof(test<T, Any, Any, Any, Any, Any, Any, Any, Any, Any>(0)) ==
         sizeof(char))
          ? 9
      : (sizeof(test<T, Any, Any, Any, Any, Any, Any, Any, Any>(0)) ==
         sizeof(char))
          ? 8
      : (sizeof(test<T, Any, Any, Any, Any, Any, Any, Any>(0)) == sizeof(char))
          ? 7
      : (sizeof(test<T, Any, Any, Any, Any, Any, Any>(0)) == sizeof(char)) ? 6
      : (sizeof(test<T, Any, Any, Any, Any, Any>(0)) == sizeof(char))      ? 5
      : (sizeof(test<T, Any, Any, Any, Any>(0)) == sizeof(char))           ? 4
      : (sizeof(test<T, Any, Any, Any>(0)) == sizeof(char))                ? 3
      : (sizeof(test<T, Any, Any>(0)) == sizeof(char))                     ? 2
      : (sizeof(test<T, Any>(0)) == sizeof(char))                          ? 1
                                                                           : 0;
};

template <typename T, typename S> S serialize(const T &obj) {
  if constexpr (HasSerialize<T>::Value) {
    return const_cast<T &>(obj).serialize();
  } else if constexpr (IsPrimitive<T>::Value) {
    S s;
    if constexpr (IsSame<T, bool>::Value)
      s.pushBool(obj);
    else if constexpr (IsSame<T, f32>::Value)
      s.pushF32(obj);
    else if constexpr (IsSame<T, f64>::Value)
      s.pushF64(obj);
    else
      s.pushVarLong((long long)obj);
    return s;
  } else if constexpr (IsSame<T, String>::Value) {
    return const_cast<String &>(obj).serialize();
  } else {
    // Ordered list serialization for structs using structured bindings
    S s;
    constexpr usz count = Arity<T>::Value;
    s.pushVarLong((long long)count);
    if constexpr (count == 0) {
    } else if constexpr (count == 1) {
      auto &[p1] = const_cast<T &>(obj);
      s += serialize(p1);
    } else if constexpr (count == 2) {
      auto &[p1, p2] = const_cast<T &>(obj);
      s += serialize(p1);
      s += serialize(p2);
    } else if constexpr (count == 3) {
      auto &[p1, p2, p3] = const_cast<T &>(obj);
      s += serialize(p1);
      s += serialize(p2);
      s += serialize(p3);
    } else if constexpr (count == 4) {
      auto &[p1, p2, p3, p4] = const_cast<T &>(obj);
      s += serialize(p1);
      s += serialize(p2);
      s += serialize(p3);
      s += serialize(p4);
    } else if constexpr (count == 5) {
      auto &[p1, p2, p3, p4, p5] = const_cast<T &>(obj);
      s += serialize(p1);
      s += serialize(p2);
      s += serialize(p3);
      s += serialize(p4);
      s += serialize(p5);
    } else if constexpr (count == 6) {
      auto &[p1, p2, p3, p4, p5, p6] = const_cast<T &>(obj);
      s += serialize(p1);
      s += serialize(p2);
      s += serialize(p3);
      s += serialize(p4);
      s += serialize(p5);
      s += serialize(p6);
    } else if constexpr (count == 7) {
      auto &[p1, p2, p3, p4, p5, p6, p7] = const_cast<T &>(obj);
      s += serialize(p1);
      s += serialize(p2);
      s += serialize(p3);
      s += serialize(p4);
      s += serialize(p5);
      s += serialize(p6);
      s += serialize(p7);
    } else if constexpr (count == 8) {
      auto &[p1, p2, p3, p4, p5, p6, p7, p8] = const_cast<T &>(obj);
      s += serialize(p1);
      s += serialize(p2);
      s += serialize(p3);
      s += serialize(p4);
      s += serialize(p5);
      s += serialize(p6);
      s += serialize(p7);
      s += serialize(p8);
    }
    // ... could extend to 16
    return s;
  }
}

template <typename T> Deserializer::operator T() {
  usz at = 0;
  return Xi::deserialize<T>(*data, at);
}

template <typename T, typename S> T deserialize(const S &s, usz &at) {
  if constexpr (HasDeserializeAt<T>::Value) {
    return T::deserialize(s, at);
  } else if constexpr (HasDeserialize<T>::Value) {
    // Falls back to non-offset deserialize but cannot update 'at' correctly
    // unless the type provides its length.
    // This is a limitation for legacy types.
    return T::deserialize(s);
  } else if constexpr (IsPrimitive<T>::Value) {
    if constexpr (IsSame<T, bool>::Value) {
      if (at < s.size())
        return (T)(s[at++] != 0);
      return T();
    } else if constexpr (IsSame<T, f32>::Value) {
      if (at + 4 > s.size())
        return f32();
      u32 r = 0;
      for (int i = 0; i < 4; i++)
        r |= (u32)s[at++] << (i * 8);
      union {
        f32 f;
        u32 i;
      } u;
      u.i = r;
      return u.f;
    } else if constexpr (IsSame<T, f64>::Value) {
      if (at + 8 > s.size())
        return f64();
      u64 r = 0;
      for (int i = 0; i < 8; i++)
        r |= (u64)s[at++] << (i * 8);
      union {
        f64 f;
        u64 i;
      } u;
      u.i = r;
      return u.f;
    } else {
      auto res = s.peekVarLong(at);
      if (res.error)
        return T();
      at += res.bytes;
      return (T)res.value;
    }
  } else if constexpr (IsSame<T, String>::Value) {
    return (T)InlineArray<u8>::deserialize(s, at);
  } else if constexpr (IsSame<T, S>::Value) {
    return (T)InlineArray<u8>::deserialize(s, at);
  } else {
    // Generic struct deserialization (matches Arity-based serialization count)
    T obj;
    auto res = s.peekVarLong(at);
    if (res.error)
      return obj;
    at += res.bytes;
    // We can't really destructure into a new object without reflection,
    // so for generic structs, this remains a stub unless they have
    // deserialize().
    return obj;
  }
}

template <typename T>
bool validToDeserialize(const T &obj, usz originalLength) {
  if constexpr (HasValidToDeserialize<T>::Value) {
    // This is likely intended for use after a serialize() call.
    // However, without the serialized data, we can't do much.
    return true;
  }
  return true;
}

template <> struct FNVHasher<String> {
  static usz fnvHash(const String &s) {
#if __SIZEOF_POINTER__ == 8
    usz h = 14695981039346656037ULL;
    const usz prime = 1099511628211ULL;
#else
    usz h = 2166136261U;
    const usz prime = 16777619U;
#endif
    const u8 *d = s.data();
    for (usz i = 0; i < s.size(); ++i) {
      h ^= (usz)d[i];
      h *= prime;
    }
    return h;
  }
};
inline void randomFill(String &s, usz len = 0) {
  if (len == 0)
    len = s.size();
  if (s.size() < len)
    len = s.size();
  u8 *raw = const_cast<u8 *>(reinterpret_cast<const u8 *>(s.data()));
  if (!raw && len > 0)
    return;
  randomFill(raw, len);
}
inline void secureRandomFill(String &s, usz len = 0) {
  if (len == 0)
    len = s.size();
  if (s.size() < len)
    len = s.size();
  u8 *raw = const_cast<u8 *>(reinterpret_cast<const u8 *>(s.data()));
  if (!raw && len > 0)
    return;
  secureRandomFill(raw, len);
}
int parseInt(const String &s);
long long parseLong(const String &s);
f64 parseDouble(const String &s);
inline void randomSeed(String str) {
  u32 h = 5381;
  int c;
  unsigned char *d = (unsigned char *)str.data();
  while ((c = *d++)) {
    h = ((h << 5) + h) + c;
  }
  randomSeed(h);
}

inline void writeVarLong(Xi::String &s, u64 v) { s.pushVarLong((long long)v); }
inline u64 readVarLong(const Xi::String &s, usz &at) {
  auto res = s.peekVarLong(at);
  if (res.error)
    return 0;
  at += res.bytes;
  return (u64)res.value;
}

} // namespace Xi
#endif