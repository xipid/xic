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
class String : public InlineArray<u8> {
private:
  static usz str_len(const char *s) {
    usz l = 0;
    if (s)
      while (s[l])
        l++;
    return l;
  }
  void append_raw(const u8 *b, usz c) { pushEach(b, c); }

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

  void append_f32(f64 n, int precision = 6) {
    if (n < 0) {
      push('-');
      n = -n;
    }
    append_int((long long)n);
    push('.');
    f64 frac = n - (long long)n;
    for (int i = 0; i < precision; i++) {
      frac *= 10;
      int digit = (int)frac;
      push(digit + '0');
      frac -= digit;
    }
  }

public:
  using InlineArray<u8>::push;
  using InlineArray<u8>::shift;
  using InlineArray<u8>::unshift;
  using InlineArray<u8>::allocate;

  /**
   * @brief Concatenates another String to this one.
   */
  void concat(const String &other) { pushEach(other.data(), other.size()); }

  String() : InlineArray<u8>() {}

  /**
   * @brief Construct from C-string.
   */
  String(const char *s) { append_raw((const u8 *)s, str_len(s)); }

  /**
   * @brief Construct from raw buffer.
   */
  String(const u8 *buffer, usz l) : InlineArray<u8>() {
    if (buffer && l > 0) {
      append_raw(buffer, l);
    }
  }

  /**
   * @brief Replaces content with data from raw address.
   * Useful for JIT/Interop.
   */
  void setFromRawAddress(unsigned long long ptrAddr, usz len) {
    destroy();
    if (len == 0 || ptrAddr == 0)
      return;
    const u8 *ptr = reinterpret_cast<const u8 *>(ptrAddr);
    this->append_raw(ptr, len);
  }

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

  String(int n) : InlineArray<u8>() { append_int(n); }
  String(long long n) : InlineArray<u8>() { append_int(n); }
  String(u64 n) : InlineArray<u8>() { append_int(n); }
  String(f64 n) : InlineArray<u8>() { append_f32(n); }
  String(f32 n) : InlineArray<u8>() { append_f32((f64)n); }

  /**
   * @brief Returns C-string representation.
   * Modifies the string to ensure null-termination if needed, then returns
   * pointer.
   */
  const char *c_str() {
    if (size() == 0)
      return "";
    push(0);
    char *ptr = (char *)data();
    pop();
    return ptr;
  }
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

  static String allocate(usz size) {
    String s;
    s.InlineArray<u8>::allocate(size);
    return s;
  }

  void fill(u8 val) {
    u8 *d = data();
    for (usz i = 0; i < size(); i++)
      d[i] = val;
  }

  long long indexOf(const char *needle, usz start = 0) const {
    return find(needle, start);
  }

  long long indexOf(char needle, usz start = 0) const {
    for (usz i = start; i < size(); i++) {
      if (operator[](i) == (u8)needle)
        return (long long)i;
    }
    return -1;
  }

  bool includes(const char *needle, usz start = 0) const {
    return find(needle, start) != -1;
  }

  bool startsWith(const char *prefix) const {
    usz pLen = str_len(prefix);
    if (pLen > size())
      return false;
    const u8 *d = data();
    const u8 *p = (const u8 *)prefix;
    for (usz i = 0; i < pLen; i++)
      if (d[i] != p[i])
        return false;
    return true;
  }

  bool endsWith(const char *suffix) const {
    usz sLen = str_len(suffix);
    if (sLen > size())
      return false;
    const u8 *d = data();
    const u8 *s = (const u8 *)suffix;
    usz start = size() - sLen;
    for (usz i = 0; i < sLen; i++)
      if (d[start + i] != s[i])
        return false;
    return true;
  }

  String slice(long long start, long long end = -1) const {
    long long len = (long long)size();
    if (start < 0)
      start = len + start;
    if (end < 0) {
      if (end == -1 && size() > 0)
        end = len;
      else
        end = len + end;
    }
    if (start < 0)
      start = 0;
    if (end > len)
      end = len;
    if (start >= end)
      return String();
    return begin((usz)start, (usz)end);
  }

  String substring(usz start, usz end = (usz)-1) const {
    if (end == (usz)-1)
      end = size();
    if (start >= end)
      return String();
    if (end > size())
      end = size();
    return begin(start, end);
  }

  String trim() const {
    if (size() == 0)
      return String();
    const u8 *d = data();
    usz s = 0;
    while (s < size() && d[s] <= ' ')
      s++;
    if (s == size())
      return String();
    usz e = size() - 1;
    while (e > s && d[e] <= ' ')
      e--;
    return begin(s, e + 1);
  }

  String toUpperCase() const {
    String res = *this;
    u8 *d = res.data();
    for (usz i = 0; i < res.size(); i++) {
      if (d[i] >= 'a' && d[i] <= 'z')
        d[i] -= 32;
    }
    return res;
  }

  String toLowerCase() const {
    String res = *this;
    u8 *d = res.data();
    for (usz i = 0; i < res.size(); i++) {
      if (d[i] >= 'A' && d[i] <= 'Z')
        d[i] += 32;
    }
    return res;
  }

  char charAt(usz idx) const {
    if (idx >= size())
      return 0;
    return (char)operator[](idx);
  }

  int charCodeAt(usz idx) const {
    if (idx >= size())
      return -1;
    return (int)operator[](idx);
  }

  String padStart(usz targetLen, char padChar = ' ') const {
    if (size() >= targetLen)
      return *this;
    String res;
    usz toAdd = targetLen - size();
    for (usz i = 0; i < toAdd; i++)
      res.push((u8)padChar);
    res += *this;
    return res;
  }

  String padEnd(usz targetLen, char padChar = ' ') const {
    if (size() >= targetLen)
      return *this;
    String res = *this;
    usz toAdd = targetLen - size();
    for (usz i = 0; i < toAdd; i++)
      res.push((u8)padChar);
    return res;
  }

  static String *create() { return new String(); }

  String &operator+=(const char *s) {
    append_raw((const u8 *)s, str_len(s));
    return *this;
  }
  String &operator+=(const String &o) {
    concat(o);
    return *this;
  }
  String &operator+=(char c) {
    push((u8)c);
    return *this;
  }
  String &operator+=(int n) {
    append_int(n);
    return *this;
  }
  String &operator+=(long long n) {
    append_int(n);
    return *this;
  }
  template <typename T>
  auto operator+=(const T &obj) -> decltype(obj.toString(), *this) {
    *this += obj.toString();
    return *this;
  }

  bool operator==(const String &other) const {
    if (this == &other)
      return true;
    if (size() != other.size())
      return false;

    const u8 *d1 = data();
    const u8 *d2 = other.data();
    if (!d1 || !d2)
      return d1 == d2;

    for (usz i = 0; i < size(); ++i)
      if (d1[i] != d2[i])
        return false;
    return true;
  }
  bool operator!=(const String &other) const { return !(*this == other); }

  bool operator==(const char *other) const {
    usz oLen = str_len(other);
    if (size() != oLen)
      return false;
    const u8 *d = data();
    for (usz i = 0; i < oLen; ++i)
      if (d[i] != (u8)other[i])
        return false;
    return true;
  }
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

  long long find(const char *needle, usz start = 0) const {
    usz nLen = str_len(needle);
    if (nLen == 0 || size() < nLen)
      return -1;
    const u8 *h = data();
    const u8 *n = (const u8 *)needle;
    for (usz i = start; i <= size() - nLen; ++i) {
      usz j = 0;
      while (j < nLen && h[i + j] == n[j])
        j++;
      if (j == nLen)
        return (long long)i;
    }
    return -1;
  }

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
  Array<String> split(const char *sep) const {
    Array<String> res;
    usz sLen = str_len(sep);
    if (sLen == 0)
      return res;

    String *mut = const_cast<String *>(this);
    usz totalLen = size();
    Array<long long> indices;
    long long pos = find(sep, 0);
    while (pos != -1) {
      indices.push(pos);
      pos = find(sep, (usz)pos + sLen);
    }

    usz curr = 0;
    usz cnt = indices.size();
    for (usz i = 0; i < cnt; i++) {
      usz idx = (usz)indices[i];
      if (idx > totalLen)
        break;
      res.push(mut->begin(curr, idx));
      curr = idx + sLen;
    }
    if (curr <= totalLen)
      res.push(mut->begin(curr, totalLen));
    return res;
  }

  String replace(const Regex &reg, const String &rep) const;
  String replace(const char *tgt, const char *rep) const {
    Array<String> parts = split(tgt);
    String res;
    bool first = true;
    for (usz i = 0; i < parts.size(); ++i) {
      if (!first)
        res += rep;
      res += parts[i];
      first = false;
    }
    return res;
  }

  int toInt() const { return Xi::parseInt(*this); }
  f64 toDouble() const { return Xi::parseDouble(*this); }

  bool constantTimeEquals(const Xi::String &b, int length = 0) const {
    const u8 *ad = data();
    const u8 *bd = b.data();
    usz actualALen = this->size();
    usz actualBLen = b.size();

    usz compareLen = (length > 0)
                         ? static_cast<usz>(length)
                         : (actualALen > actualBLen ? actualALen : actualBLen);

    u8 result = 0;

    if (length > 0 && (actualALen < (usz)length || actualBLen < (usz)length)) {
      result = 1;
    } else if (length == 0 && actualALen != actualBLen) {
      result = 1;
    }

    for (usz i = 0; i < compareLen; ++i) {
      u8 aByte = (i < actualALen) ? ad[i] : 0;
      u8 bByte = (i < actualBLen) ? bd[i] : 0;
      result |= (aByte ^ bByte);
    }

    return result == 0;
  }

  String *pushVarLong(long long v) {
    unsigned long long n = (unsigned long long)v;
    do {
      u8 t = n & 0x7f;
      n >>= 7;
      if (n)
        t |= 0x80;
      push(t);
    } while (n);
    return this;
  }

  long long shiftVarLong() {
    unsigned long long r = 0;
    int s = 0;
    u8 b;
    do {
      if (s >= 70 || size() == 0)
        return 0;
      b = shift();
      r |= (unsigned long long)(b & 0x7f) << s;
      s += 7;
    } while (b & 0x80);
    return (long long)r;
  }

  String *unshiftVarLong(long long v) {
    unsigned long long n = (unsigned long long)v;
    InlineArray<u8> temp; // Use InlineArray as temp buffer
    do {
      u8 t = n & 0x7f;
      n >>= 7;
      if (n)
        t |= 0x80;
      temp.push(t);
    } while (n);
    for (long long i = (long long)temp.size() - 1; i >= 0; i--)
      unshift(temp[i]);
    return this;
  }
  String *unshiftVarLong() { return unshiftVarLong((long long)size()); }

  void pushVarString(const String &s) {
    pushVarLong((long long)s.size());
    append_raw(const_cast<String &>(s).data(), s.size());
  }

  String shiftVarString() {
    auto res = peekVarLong();
    if (res.error)
      return String();
    if (size() < (usz)(res.bytes + res.value))
      return String();

    // Consume header
    for (int i = 0; i < res.bytes; i++)
      shift();

    // Extract payload
    String result = begin(0, (usz)res.value);

    // Consume payload
    for (long long i = 0; i < res.value; i++)
      shift();

    return result;
  }

  String *pushBool(bool v) {
    push(v ? 1 : 0);
    return this;
  }
  bool shiftBool() { return size() > 0 ? (shift() != 0) : false; }

  String *pushI64(long long v) {
    for (int i = 0; i < 8; i++)
      push((u8)((v >> (i * 8)) & 0xff));
    return this;
  }
  long long shiftI64() {
    if (size() < 8)
      return 0;
    unsigned long long r = 0;
    for (int i = 0; i < 8; i++)
      r |= (unsigned long long)shift() << (i * 8);
    return (long long)r;
  }

  String *pushF64(f64 v) {
    union {
      f64 f;
      long long i;
    } u;
    u.f = v;
    return pushI64(u.i);
  }
  f64 shiftF64() {
    union {
      f64 f;
      long long i;
    } u;
    u.i = shiftI64();
    return u.f;
  }

  String *pushF32(f32 v) {
    union {
      f32 f;
      u32 i;
    } u;
    u.f = v;
    for (int i = 0; i < 4; i++)
      push((u8)((u.i >> (i * 8)) & 0xff));
    return this;
  }
  f32 shiftF32() {
    if (size() < 4)
      return 0.0f;
    u32 r = 0;
    for (int i = 0; i < 4; i++)
      r |= (u32)shift() << (i * 8);
    union {
      f32 f;
      u32 i;
    } u;
    u.i = r;
    return u.f;
  }

  VarLongResult peekVarLong(usz offset = 0) const {
    unsigned long long r = 0;
    int s = 0;
    int i = 0;
    if (offset >= size())
      return {0, 0, true};
    const u8 *d = data();
    for (i = (int)offset; i < (int)size(); ++i) {
      u8 b = d[i];
      r |= (unsigned long long)(b & 0x7f) << s;
      s += 7;
      if (!(b & 0x80))
        return {(long long)r, (i - (int)offset) + 1, false};
      if (s >= 70)
        break;
    }
    return {0, 0, true};
  }

  Xi::String toDeci() const {
    Xi::String result;
    for (usz i = 0; i < size(); ++i) {
      u8 value = (*this)[i];
      if (value == 0)
        result.push('0');
      else {
        char buffer[4];
        int pos = 0;
        while (value > 0) {
          buffer[pos++] = (value % 10) + '0';
          value /= 10;
        }
        for (int j = pos - 1; j >= 0; --j)
          result.push(buffer[j]);
      }
      if (i < size() - 1)
        result.push(' ');
    }
    return result;
  }

  static void check_abi() {}
};

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
inline int parseInt(const String &s) {
  const u8 *d = const_cast<String &>(s).data();
  usz length = s.size();
  if (length == 0 || !d)
    return 0;
  int result = 0;
  int sign = (d[0] == '-') ? -1 : 1;
  usz i = (d[0] == '-' || d[0] == '+') ? 1 : 0;
  for (; i < length; ++i) {
    if (d[i] >= '0' && d[i] <= '9')
      result = result * 10 + (d[i] - '0');
    else
      break;
  }
  return result * sign;
}
inline long long parseLong(const String &s) {
  const u8 *d = const_cast<String &>(s).data();
  usz length = s.size();
  if (length == 0 || !d)
    return 0;
  long long result = 0;
  long long sign = (d[0] == '-') ? -1 : 1;
  usz i = (d[0] == '-' || d[0] == '+') ? 1 : 0;
  for (; i < length; ++i) {
    if (d[i] >= '0' && d[i] <= '9')
      result = result * 10 + (d[i] - '0');
    else
      break;
  }
  return result * sign;
}
inline f64 parseDouble(const String &s) {
  const u8 *d = const_cast<String &>(s).data();
  usz length = s.size();
  if (length == 0 || !d)
    return 0.0;
  f64 result = 0.0;
  f64 sign = (d[0] == '-') ? -1.0 : 1.0;
  usz i = (d[0] == '-' || d[0] == '+') ? 1 : 0;
  while (i < length && d[i] >= '0' && d[i] <= '9') {
    result = result * 10.0 + (d[i] - '0');
    i++;
  }
  if (i < length && d[i] == '.') {
    i++;
    f64 weight = 0.1;
    while (i < length && d[i] >= '0' && d[i] <= '9') {
      result += (d[i] - '0') * weight;
      weight /= 10.0;
      i++;
    }
  }
  if (i < length && (d[i] == 'e' || d[i] == 'E')) {
    i++;
    int expSign = 1;
    if (i < length && d[i] == '-') {
      expSign = -1;
      i++;
    } else if (i < length && d[i] == '+')
      i++;
    int expVal = 0;
    while (i < length && d[i] >= '0' && d[i] <= '9') {
      expVal = expVal * 10 + (d[i] - '0');
      i++;
    }
    f64 factor = 1.0;
    f64 base = 10.0;
    int p = expVal;
    while (p > 0) {
      if (p & 1)
        factor *= base;
      base *= base;
      p >>= 1;
    }
    if (expSign == -1)
      result /= factor;
    else
      result *= factor;
  }
  return result * sign;
}
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