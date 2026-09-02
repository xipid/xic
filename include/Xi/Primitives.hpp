/**
 * @file Primitives.hpp
 * @brief Core type definitions and metaprogramming utilities for the Xi
 * framework.

 */

#ifndef XI_CORE_PRIMITIVES_HPP
#define XI_CORE_PRIMITIVES_HPP

namespace Collection {
class String;
}

#if !defined(__KERNEL__) && !defined(XI_NO_STD) && !defined(MECA_EMBEDDED)
#include <time.h>
#endif

#ifdef __KERNEL__
#include <linux/delay.h>
#endif

/**
 * @def XI_EXPORT
 * @brief Macro for exporting symbols (handles Cheerp, bindgen, and standard
 * C++).
 */
#ifdef __cheerp__
#define XI_EXPORT [[cheerp::jsexport]]
#elif defined(__BINDGEN__)
#define XI_EXPORT __attribute__((annotate("XI_EXPORT")))
#else
#define XI_EXPORT
#endif

#if !defined(__KERNEL__) && !defined(XI_NO_STD)
#if defined(__has_include)
#if __has_include(<new>)
#include <new>
#define __PLACEMENT_NEW_INLINE
#endif
#elif defined(__cplusplus) && __cplusplus >= 201103L
#include <new>
#define __PLACEMENT_NEW_INLINE
#endif
#endif

/**
 * @namespace Xi
 * @brief The core namespace for the Xi framework.
 */
namespace Xi {

/** @typedef usz
 *  @brief Unsigned size type (equivalent to size_t).
 */
using usz = decltype(sizeof(0));

using u8 = unsigned char; ///< Unsigned 8-bit integer.
using i8 = signed char;   ///< Signed 8-bit integer.

// Auto-detect integer sizes for 16/32 bit types
#if __SIZEOF_INT__ == 2
using u16 = unsigned int;  ///< Unsigned 16-bit integer.
using i16 = int;           ///< Signed 16-bit integer.
using u32 = unsigned long; ///< Unsigned 32-bit integer.
using i32 = long;          ///< Signed 32-bit integer.
#else
using u16 = unsigned short; ///< Unsigned 16-bit integer.
using i16 = short;          ///< Signed 16-bit integer.
using u32 = unsigned int;   ///< Unsigned 32-bit integer.
using i32 = int;            ///< Signed 32-bit integer.
#endif

using u64 = unsigned long long; ///< Unsigned 64-bit integer.
using i64 = long long;          ///< Signed 64-bit integer.

using f32 = float;  ///< 32-bit floating point.
using f64 = double; ///< 64-bit floating point.

/** @var null
 *  @brief Type-safe null pointer constant.
 */
static constexpr decltype(nullptr) null = nullptr;

// -------------------------------------------------------------------------
// Metaprogramming Utilities
// -------------------------------------------------------------------------

/**
 * @struct RemoveRef
 * @brief Removes reference qualifier from a type.
 */
template <typename T> struct RemoveRef {
  using Type = T;
};
template <typename T> struct RemoveRef<T &> {
  using Type = T;
};
template <typename T> struct RemoveRef<T &&> {
  using Type = T;
};

/**
 * @brief Casts an lvalue to an rvalue (equivalent to std::move).
 */
template <typename T>
inline typename RemoveRef<T>::Type &&Move(T &&arg) noexcept {
  return static_cast<typename RemoveRef<T>::Type &&>(arg);
}

/**
 * @struct EnableIf
 * @brief SFINAE helper for conditional template instantiation.
 */
template <bool B, typename T = void> struct EnableIf {};
template <typename T> struct EnableIf<true, T> {
  using Type = T;
};

/**
 * @brief Swaps the values of two objects.
 */
template <typename T> inline void Swap(T &a, T &b) {
  T temp = Xi::Move(a);
  a = Xi::Move(b);
  b = Xi::Move(temp);
}

/**
 * @brief Returns an rvalue reference to a type without constructing it.
 */
template <typename T> T &&DeclVal() noexcept;

/**
 * @struct IsSame
 * @brief Checks if two types are identical.
 */
template <typename U, typename V> struct IsSame {
  static const bool Value = false;
};
template <typename U> struct IsSame<U, U> {
  static const bool Value = true;
};

template <typename T> struct RemoveConst { using Type = T; };
template <typename T> struct RemoveConst<const T> { using Type = T; };

template <typename T> struct Decay {
  using Type = typename RemoveConst<typename RemoveRef<T>::Type>::Type;
};

template <typename... Ts> struct MaxSize;
template <typename T> struct MaxSize<T> { static constexpr usz Value = sizeof(T); };
template <typename T, typename... Ts> struct MaxSize<T, Ts...> {
  static constexpr usz rest = MaxSize<Ts...>::Value;
  static constexpr usz Value = sizeof(T) > rest ? sizeof(T) : rest;
};

template <typename... Ts> struct MaxAlign;
template <typename T> struct MaxAlign<T> { static constexpr usz Value = alignof(T); };
template <typename T, typename... Ts> struct MaxAlign<T, Ts...> {
  static constexpr usz rest = MaxAlign<Ts...>::Value;
  static constexpr usz Value = alignof(T) > rest ? alignof(T) : rest;
};

template <typename T, typename... Ts> struct TypeIndex;
template <typename T, typename... Ts> struct TypeIndex<T, T, Ts...> {
  static constexpr u32 Value = 0;
};
template <typename T, typename U, typename... Ts> struct TypeIndex<T, U, Ts...> {
  static constexpr u32 Value = 1 + TypeIndex<T, Ts...>::Value;
};

template <typename... Ts>
class Either {
  alignas(MaxAlign<Ts...>::Value) u8 data[MaxSize<Ts...>::Value];
  i32 type_id = -1;
  void (*destroy_fn)(void*) = nullptr;
  void (*copy_fn)(void*, const void*) = nullptr;
  void (*move_fn)(void*, void*) = nullptr;

public:
  Either() {}

  template <typename T, typename = typename EnableIf<!IsSame<typename Decay<T>::Type, Either>::Value>::Type>
  Either(T&& val) {
    using Decayed = typename Decay<T>::Type;
    type_id = (i32)TypeIndex<Decayed, Ts...>::Value;
    new (data) Decayed(Xi::Move(val));
    destroy_fn = [](void* ptr) { static_cast<Decayed*>(ptr)->~Decayed(); };
    copy_fn = [](void* dst, const void* src) { new (dst) Decayed(*static_cast<const Decayed*>(src)); };
    move_fn = [](void* dst, void* src) { new (dst) Decayed(Xi::Move(*static_cast<Decayed*>(src))); };
  }

  Either(const Either& other) : type_id(other.type_id), destroy_fn(other.destroy_fn), copy_fn(other.copy_fn), move_fn(other.move_fn) {
    if (copy_fn) copy_fn(data, other.data);
  }

  Either(Either&& other) : type_id(other.type_id), destroy_fn(other.destroy_fn), copy_fn(other.copy_fn), move_fn(other.move_fn) {
    if (move_fn) move_fn(data, other.data);
    other.type_id = -1;
    other.destroy_fn = nullptr;
    other.copy_fn = nullptr;
    other.move_fn = nullptr;
  }

  Either& operator=(const Either& other) {
    if (this != &other) {
      if (destroy_fn) destroy_fn(data);
      type_id = other.type_id;
      destroy_fn = other.destroy_fn;
      copy_fn = other.copy_fn;
      move_fn = other.move_fn;
      if (copy_fn) copy_fn(data, other.data);
    }
    return *this;
  }

  Either& operator=(Either&& other) {
    if (this != &other) {
      if (destroy_fn) destroy_fn(data);
      type_id = other.type_id;
      destroy_fn = other.destroy_fn;
      copy_fn = other.copy_fn;
      move_fn = other.move_fn;
      if (move_fn) move_fn(data, other.data);
      other.type_id = -1;
      other.destroy_fn = nullptr;
      other.copy_fn = nullptr;
      other.move_fn = nullptr;
    }
    return *this;
  }

  ~Either() {
    if (destroy_fn) destroy_fn(data);
  }

  template <typename T>
  bool is() const {
    return type_id == (i32)TypeIndex<T, Ts...>::Value;
  }

  template <typename T>
  T& get() {
    return *reinterpret_cast<T*>(data);
  }

  template <typename T>
  const T& get() const {
    return *reinterpret_cast<const T*>(data);
  }
};

/**
 * @struct Equal
 * @brief Generic equality comparator.
 */
template <typename T> struct Equal {
  static bool eq(const T &a, const T &b) { return a == b; }
};

/**
 * @brief Specialization for C-style Collection::Strings.
 */
template <> struct Equal<const char *> {
  static bool eq(const char *a, const char *b) {
    if (a == b)
      return true;
    if (!a || !b)
      return false;
    while (*a && *b) {
      if (*a != *b)
        return false;
      a++;
      b++;
    }
    return *a == *b;
  }
};

// --- Mathematical Constants ---
#ifndef PI
static constexpr f64 PI = 3.14159265358979323846;
#endif
static constexpr f64 E = 2.71828182845904523536;

/**
 * @struct FNVHasher
 * @brief Fowler-Noll-Vo (FNV) hash implementation.
 */
template <typename T> struct FNVHasher {
  static usz fnvHash(const T &key) {
    const char *ptr = (const char *)&key;
#if __SIZEOF_POINTER__ == 8
    usz fnvHash = 14695981039346656037ULL;
    const usz prime = 1099511628211ULL;
#else
    usz fnvHash = 2166136261U;
    const usz prime = 16777619U;
#endif
    for (usz i = 0; i < sizeof(T); ++i) {
      fnvHash ^= (usz)((u8)ptr[i]);
      fnvHash *= prime;
    }
    return fnvHash;
  }
};

/**
 * @brief Specialization for raw pointers using Murmur3 mixer.
 */
template <typename T> struct FNVHasher<T *> {
  static usz fnvHash(T *key) {
    usz k = (usz)key;
#if __SIZEOF_POINTER__ == 8
    k ^= k >> 33;
    k *= 0xff51afd7ed558ccdULL;
    k ^= k >> 33;
    k *= 0xc4ceb9fe1a85ec53ULL;
    k ^= k >> 33;
#else
    k ^= k >> 16;
    k *= 0x85ebca6b;
    k ^= k >> 13;
    k *= 0xc2b2ae35;
    k ^= k >> 16;
#endif
    return k;
  }
};

/**
 * @brief Mixes a hash value for better distribution.
 */
usz fnvHashMix(usz k);

/**
 * @class IMemoryDevice
 * @brief Interface for memory management devices (CPU/GPU).
 */
class XI_EXPORT IMemoryDevice {
public:
  virtual void *alloc(usz size) = 0;
  virtual void free(void *handle) = 0;
  virtual void upload(void *handle, const void *src, usz size) = 0;
  virtual void download(void *handle, void *dst, usz size) = 0;
  virtual void *view(void *handle, i32 type = 0) = 0;
  virtual void *allocSurface(i32 w, i32 h, i32 channels = 4) = 0;
  virtual ~IMemoryDevice() = default;
};

class XI_EXPORT MemoryDevice : public IMemoryDevice {
public:
  virtual void *alloc(usz size) override = 0;
  virtual void free(void *handle) override = 0;
  virtual void upload(void *handle, const void *src, usz size) override = 0;
  virtual void download(void *handle, void *dst, usz size) override = 0;

  virtual void *view(void *handle, i32 type = 0) override {
    (void)type;
    return handle;
  }

  virtual void *allocSurface(i32 w, i32 h, i32 channels = 4) override {
    return alloc((usz)(w * h * channels));
  }

  virtual ~MemoryDevice() = default;
};

// -------------------------------------------------------------------------
// Serialization Traits & Helpers
// -------------------------------------------------------------------------

/**
 * @struct HasSerialize
 * @brief Detects if a type has a .serialize() method.
 */
template <typename T> struct HasSerialize {
private:
  template <typename U>
  static auto test(int)
      -> decltype(Xi::Move(Xi::DeclVal<U>().serialize()), char());
  template <typename U> static long test(...);

public:
  static const bool Value = sizeof(test<T>(0)) == sizeof(char);
};

/**
 * @struct HasDeserialize
 * @brief Detects if a type has a static .deserialize(Collection::String)
 * method.
 */
template <typename T> struct HasDeserialize {
private:
  template <typename U>
  static auto test(int)
      -> decltype(U::deserialize(Xi::DeclVal<Collection::String>()), char());
  template <typename U> static long test(...);

public:
  static const bool Value = sizeof(test<T>(0)) == sizeof(char);
};

/**
 * @struct HasDeserializeAt
 * @brief Detects if a type has a static .deserialize(Collection::String, usz&)
 * method.
 */
template <typename T> struct HasDeserializeAt {
private:
  template <typename U>
  static auto test(int)
      -> decltype(U::deserialize(Xi::DeclVal<Collection::String>(),
                                 Xi::DeclVal<usz &>()),
                  char());
  template <typename U> static long test(...);

public:
  static const bool Value = sizeof(test<T>(0)) == sizeof(char);
};

/**
 * @struct IsPrimitive
 * @brief Identifies built-in numeric/boolean types.
 */
template <typename T> struct IsPrimitive {
  static const bool Value = false;
};

#define XI_MARK_PRIMITIVE(Ty)                                                  \
  template <> struct IsPrimitive<Ty> {                                         \
    static const bool Value = true;                                            \
  };

XI_MARK_PRIMITIVE(bool)
XI_MARK_PRIMITIVE(u8)
XI_MARK_PRIMITIVE(i8)
XI_MARK_PRIMITIVE(u16)
XI_MARK_PRIMITIVE(i16)
XI_MARK_PRIMITIVE(u32)
XI_MARK_PRIMITIVE(i32)
XI_MARK_PRIMITIVE(u64)
XI_MARK_PRIMITIVE(i64)
XI_MARK_PRIMITIVE(f32)
XI_MARK_PRIMITIVE(f64)

/**
 * @brief Serializes an object to a Collection::String.
 */
template <typename T, typename S = Collection::String>
S serialize(const T &obj);

/**
 * @brief Checks if a Collection::String is valid to deserialize into a type.
 */
template <typename T> bool validToDeserialize(const T &obj, usz originalLength);

struct Deserializer;

/**
 * @brief Deserializes a Collection::String into an object.
 */
template <typename S = Collection::String>
inline Deserializer deserialize(const S &s);

/**
 * @brief Deserializes a Collection::String into an object from a specific
 * offset.
 */
template <typename T, typename S = Collection::String>
T deserialize(const S &s, usz &at);

// -------------------------------------------------------------------------
// Time Primitives
// -------------------------------------------------------------------------

/**
 * @brief Returns current time in milliseconds.
 */
i64 millis();

/**
 * @brief Returns current time in microseconds.
 */
i64 micros();

/** @var systemStartMicros
 *  @brief Global epoch offset for time synchronization.
 */
#if __cplusplus >= 201703L || (defined(_MSVC_LANG) && _MSVC_LANG >= 201703L)
inline i64 systemStartMicros = 0;
#else
extern i64 systemStartMicros;
#endif

// Specialized Hashers
template <> struct FNVHasher<u32> {
  static usz fnvHash(const u32 &k) { return fnvHashMix((usz)k); }
};
template <> struct FNVHasher<int> {
  static usz fnvHash(const int &k) { return fnvHashMix((usz)k); }
};
template <> struct FNVHasher<u64> {
  static usz fnvHash(const u64 &k) { return fnvHashMix((usz)k); }
};
template <> struct FNVHasher<const char *> {
  static usz fnvHash(const char *key) {
#if __SIZEOF_POINTER__ == 8
    usz fnvHash = 14695981039346656037ULL;
    const usz prime = 1099511628211ULL;
#else
    usz fnvHash = 2166136261U;
    const usz prime = 16777619U;
#endif
    while (*key) {
      fnvHash ^= (usz)((u8)*key++);
      fnvHash *= prime;
    }
    return fnvHash;
  }
};

i64 epochMicros();

int getGMT();

inline int parse_int(const char *&str, int len) {
  int v = 0;
  for (int i = 0; i < len; ++i) {
    char c = *str;
    if (c >= '0' && c <= '9') {
      v = v * 10 + (c - '0');
      str++;
    } else
      break;
  }
  return v;
}

inline bool ch_eq(char a, char b) {
  if (a >= 'A' && a <= 'Z')
    a += 32;
  if (b >= 'A' && b <= 'Z')
    b += 32;
  return a == b;
}

inline void sleepU(u64 us) {
#if defined(__KERNEL__)
  if (us >= 20000) {
    msleep(static_cast<unsigned int>(us / 1000));
  } else if (us > 0) {
    usleep_range(static_cast<unsigned long>(us), static_cast<unsigned long>(us));
  }
#elif defined(FREERTOS_CONFIG_H) || defined(INC_FREERTOS_H) || defined(ESP_PLATFORM)
  #if defined(ESP_PLATFORM)
    vTaskDelay(us / 1000 / portTICK_PERIOD_MS);
  #else
    TickType_t xDelay =
        static_cast<TickType_t>(us / 1000000 * configTICK_RATE_HZ);
    if (xDelay == 0 && us > 0)
      xDelay = 1;
    vTaskDelay(xDelay);
  #endif
#elif defined(XI_NO_STD) || defined(MECA_EMBEDDED) || defined(__XTENSA__) || defined(__riscv)
  i64 start = micros();
  while (micros() - start < (i64)us) {
    // busy wait
  }
#elif defined(__KERNEL__)
  udelay(us);
#elif defined(ARDUINO)
  delayMicroseconds(us);
#elif defined(_WIN32)
  ::Sleep(static_cast<DWORD>(us / 1000));
#else
  struct timespec ts;
  ts.tv_sec = static_cast<long>(us / 1000000);
  ts.tv_nsec = static_cast<long>((us % 1000000) * 1000);
  nanosleep(&ts, nullptr);
#endif
}

inline void sleep(double seconds) { sleepU(seconds * 1000000); }

inline void sleepM(u64 ms) { sleepU(ms * 1000); }

} // namespace Xi

#ifndef __PLACEMENT_NEW_INLINE
#define __PLACEMENT_NEW_INLINE
inline void *operator new(Xi::usz, void *p) noexcept { return p; }
inline void operator delete(void *, void *) noexcept {}
#endif

#endif // XI_CORE_PRIMITIVES_HPP