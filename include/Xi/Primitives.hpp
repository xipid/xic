// include/Xi/Primitives.hpp

#ifndef XI_PRIMITIVES
#define XI_PRIMITIVES

#ifdef __cheerp__
#define XI_EXPORT [[cheerp::jsexport]]
#elif defined(__BINDGEN__)
#define XI_EXPORT __attribute__((annotate("XI_EXPORT")))
#else
#define XI_EXPORT
#endif

#include <ctime>
#if defined(__has_include)
#if __has_include(<new>)
#include <new>
#define __PLACEMENT_NEW_INLINE
#endif
#elif defined(__cplusplus) && __cplusplus >= 201103L
#include <new>
#define __PLACEMENT_NEW_INLINE
#endif

namespace Xi {
using usz = decltype(sizeof(0));

using u8 = unsigned char;
using i8 = signed char;

// Auto-detect integer sizes for 16/32 bit types
#if __SIZEOF_INT__ == 2
using u16 = unsigned int;
using i16 = int;
using u32 = unsigned long;
using i32 = long;
#else
using u16 = unsigned short;
using i16 = short;
using u32 = unsigned int;
using i32 = int;
#endif

using u64 = unsigned long long;
using i64 = long long;

using f32 = float;
using f64 = double;

static constexpr decltype(nullptr) null = nullptr;

// -------------------------------------------------------------------------
// Metaprogramming Utilities
// -------------------------------------------------------------------------
template <typename T> struct RemoveRef {
  using Type = T;
};
template <typename T> struct RemoveRef<T &> {
  using Type = T;
};
template <typename T> struct RemoveRef<T &&> {
  using Type = T;
};

template <typename T>
inline typename RemoveRef<T>::Type &&
Move(T &&arg) noexcept // Added noinline/noexcept
{
  return static_cast<typename RemoveRef<T>::Type &&>(arg);
}

// Custom EnableIf to avoid <type_traits>
template <bool B, typename T = void> struct EnableIf {};
template <typename T> struct EnableIf<true, T> {
  using Type = T;
};

template <typename T> inline void Swap(T &a, T &b) {
  T temp = Xi::Move(a);
  a = Xi::Move(b);
  b = Xi::Move(temp);
}

template <typename T> T &&DeclVal() noexcept;

template <typename U, typename V> struct IsSame {
  static const bool Value = false;
};
template <typename U> struct IsSame<U, U> {
  static const bool Value = true;
};

template <typename T> struct Equal {
  static bool eq(const T &a, const T &b) { return a == b; }
};
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

// --- Constantes ---
#ifndef PI
static constexpr f64 PI = 3.14159265358979323846;
#endif
static constexpr f64 E = 2.71828182845904523536;

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

// Specialization for raw pointers (Murmur3 Mixer)
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

usz fnvHashMix(usz k);

class IMemoryDevice {
public:
    virtual void* alloc(usz size) = 0;
    virtual void  free(void* handle) = 0;
    virtual void  upload(void* handle, const void* src, usz size) = 0;
    virtual void  download(void* handle, void* dst, usz size) = 0;
    virtual void* view(void* handle, i32 type = 0) = 0;
    virtual void* allocSurface(i32 w, i32 h, i32 channels = 4) = 0;
    virtual ~IMemoryDevice() = default;
};

// -------------------------------------------------------------------------
// Serialization Traits & Helpers
// -------------------------------------------------------------------------

class String;

template <typename T> struct HasSerialize {
private:
  template <typename U>
  static auto test(int) -> decltype(Move(DeclVal<U>().serialize()), char());
  template <typename U> static long test(...);

public:
  static const bool Value = sizeof(test<T>(0)) == sizeof(char);
};

template <typename T> struct HasDeserialize {
private:
  template <typename U>
  static auto test(int) -> decltype(U::deserialize(DeclVal<String>()), char());
  template <typename U> static long test(...);

public:
  static const bool Value = sizeof(test<T>(0)) == sizeof(char);
};

template <typename T> struct HasDeserializeAt {
private:
  template <typename U>
  static auto test(int) -> decltype(U::deserialize(DeclVal<String>(), DeclVal<usz &>()), char());
  template <typename U> static long test(...);

public:
  static const bool Value = sizeof(test<T>(0)) == sizeof(char);
};

template <typename T> struct HasValidToDeserialize {
private:
  template <typename U>
  static auto test(int) -> decltype(Xi::DeclVal<U>().validToDeserialize(
                                       Xi::DeclVal<String>(), Xi::DeclVal<usz>()),
                                   char());
  template <typename U> static long test(...);

public:
  static const bool Value = sizeof(test<T>(0)) == sizeof(char);
};

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
 * @brief Serializes an object to a String.
 * Calls obj.serialize() if available, or handles primitives and structs.
 */
template <typename T, typename S = String> S serialize(const T &obj);

/**
 * @brief Checks if a String is valid to deserialize into a type.
 */
template <typename T> bool validToDeserialize(const T &obj, usz originalLength);

/**
 * @brief Helper for deserialize inference.
 */
struct Deserializer {
  String *data;
  template <typename T> operator T();
};

/**
 * @brief Deserializes a String into an object.
 */
template <typename S = String> inline Deserializer deserialize(const S &s) {
  return {const_cast<S *>(&s)};
}

/**
 * @brief Deserializes a String into an object from a specific offset.
 */
template <typename T, typename S = String> T deserialize(const S &s, usz &at);

// -------------------------------------------------------------------------
// Time Primitives
// -------------------------------------------------------------------------

i64 millis();
i64 micros();

// Global Epoch Offset (controlled by Spatial / Time Sync)
#if __cplusplus >= 201703L || (defined(_MSVC_LANG) && _MSVC_LANG >= 201703L)
static inline i64 systemStartMicros = 0;
#else
static i64 systemStartMicros = 0;
#endif

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

} // namespace Xi

#ifndef __PLACEMENT_NEW_INLINE
#define __PLACEMENT_NEW_INLINE
inline void *operator new(decltype(sizeof(0)), void *p) noexcept { return p; }
inline void operator delete(void *, void *) noexcept {}
#endif

#endif // XI_PRIMITIVES