/**
 * @file Primitives.cpp
 * @brief Implementation of core primitive utilities (time, hashing).

 */

#include "../../include/Xi/Primitives.hpp"

#if defined(ESP_PLATFORM)
#include "esp_timer.h"
#elif defined(_WIN32)
#include <windows.h>
#elif defined(__KERNEL__)
#include <linux/timekeeping.h>
#include <linux/slab.h>
#include <linux/string.h>
#else
  #if !defined(XI_NO_STD)
  #include <time.h>
  #endif
#endif

namespace Xi {

i64 millis() {
#if defined(__KERNEL__)
  return ktime_get_ns() / 1000000ULL;
#elif defined(ARDUINO)
  return ::millis();
#elif defined(ESP_PLATFORM)
  return esp_timer_get_time() / 1000ULL;
#elif defined(_WIN32)
  return ::GetTickCount();
#elif defined(XI_NO_STD) && defined(__linux__)
  #if defined(__x86_64__)
    struct {
      long long tv_sec;
      long long tv_nsec;
    } ts;
    long ret;
    __asm__ volatile (
      "syscall"
      : "=a"(ret)
      : "a"(228), "D"(1), "S"(&ts)
      : "rcx", "r11", "memory"
    );
    if (ret == 0) {
      return (i64)(ts.tv_sec * 1000 + ts.tv_nsec / 1000000);
    }
  #endif
  return 0;
#else
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (i64)(ts.tv_sec * 1000 + ts.tv_nsec / 1000000);
#endif
}

i64 micros() {
#if defined(__KERNEL__)
  return ktime_get_ns() / 1000ULL;
#elif defined(ARDUINO)
  return ::micros();
#elif defined(ESP_PLATFORM)
  return esp_timer_get_time();
#elif defined(_WIN32)
  static long long freq = 0;
  if (freq == 0)
    ::QueryPerformanceFrequency((LARGE_INTEGER *)&freq);
  long long counter;
  ::QueryPerformanceCounter((LARGE_INTEGER *)&counter);
  return (i64)(counter * 1000000 / freq);
#elif defined(XI_NO_STD) && defined(__linux__)
  #if defined(__x86_64__)
    struct {
      long long tv_sec;
      long long tv_nsec;
    } ts;
    long ret;
    __asm__ volatile (
      "syscall"
      : "=a"(ret)
      : "a"(228), "D"(1), "S"(&ts)
      : "rcx", "r11", "memory"
    );
    if (ret == 0) {
      return (i64)(ts.tv_sec * 1000000 + ts.tv_nsec / 1000);
    }
  #endif
  return 0;
#else
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (i64)(ts.tv_sec * 1000000 + ts.tv_nsec / 1000);
#endif
}

usz fnvHashMix(usz k) {
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

} // namespace Xi

#if defined(XI_NO_STD) && !defined(__KERNEL__)
#if defined(__linux__) && defined(__x86_64__)
extern "C" void* mmap_alloc(decltype(sizeof(0)) size) {
  long long ret;
  __asm__ volatile (
    "movq %5, %%r10\n\t"
    "movq %6, %%r8\n\t"
    "movq %7, %%r9\n\t"
    "syscall"
    : "=a"(ret)
    : "a"(9), "D"(0), "S"(size), "d"(3) // PROT_READ | PROT_WRITE
    , "r"(0x22) // MAP_PRIVATE | MAP_ANONYMOUS
    , "r"(-1)   // fd
    , "r"(0)    // offset
    : "rcx", "r11", "r10", "r8", "r9", "memory"
  );
  if (ret < 0 && ret >= -4095) {
    return nullptr;
  }
  return (void*)ret;
}

extern "C" void mmap_free(void* ptr, decltype(sizeof(0)) size) {
  if (!ptr) return;
  long ret;
  __asm__ volatile (
    "syscall"
    : "=a"(ret)
    : "a"(11), "D"(ptr), "S"(size)
    : "rcx", "r11", "memory"
  );
}
#else
extern "C" void* malloc(decltype(sizeof(0)) size);
extern "C" void free(void* ptr);
#endif
#endif

#if defined(__KERNEL__) || defined(XI_NO_STD)
void* operator new(decltype(sizeof(0)) size) {
  decltype(sizeof(0)) total_size = size + 16;
  void* ptr = nullptr;
#if defined(__KERNEL__)
  ptr = kmalloc(total_size, GFP_KERNEL);
#elif defined(XI_NO_STD) && defined(__linux__) && defined(__x86_64__)
  ptr = mmap_alloc(total_size);
#else
  ptr = malloc(total_size);
#endif
  if (!ptr) return nullptr;
  *(decltype(sizeof(0))*)ptr = total_size;
  return (void*)((char*)ptr + 16);
}

void operator delete(void* ptr) noexcept {
  if (!ptr) return;
  void* real_ptr = (char*)ptr - 16;
  decltype(sizeof(0)) total_size = *(decltype(sizeof(0))*)real_ptr;
#if defined(__KERNEL__)
  kfree(real_ptr);
#elif defined(XI_NO_STD) && defined(__linux__) && defined(__x86_64__)
  mmap_free(real_ptr, total_size);
#else
  free(real_ptr);
#endif
}

void* operator new[](decltype(sizeof(0)) size) {
  return ::operator new(size);
}

void operator delete[](void* ptr) noexcept {
  ::operator delete(ptr);
}
#endif

#if defined(XI_NO_STD) && !defined(__KERNEL__)
extern "C" {
  void* memcpy(void* dest, const void* src, decltype(sizeof(0)) n) {
    char* d = (char*)dest;
    const char* s = (const char*)src;
    for (decltype(sizeof(0)) i = 0; i < n; ++i) d[i] = s[i];
    return dest;
  }
  void* memset(void* s, int c, decltype(sizeof(0)) n) {
    char* p = (char*)s;
    for (decltype(sizeof(0)) i = 0; i < n; ++i) p[i] = (char)c;
    return s;
  }
  void* memmove(void* dest, const void* src, decltype(sizeof(0)) n) {
    char* d = (char*)dest;
    const char* s = (const char*)src;
    if (d < s) {
      for (decltype(sizeof(0)) i = 0; i < n; ++i) d[i] = s[i];
    } else if (d > s) {
      for (decltype(sizeof(0)) i = n; i > 0; --i) d[i-1] = s[i-1];
    }
    return dest;
  }
  int memcmp(const void* s1, const void* s2, decltype(sizeof(0)) n) {
    const unsigned char* p1 = (const unsigned char*)s1;
    const unsigned char* p2 = (const unsigned char*)s2;
    for (decltype(sizeof(0)) i = 0; i < n; ++i) {
      if (p1[i] < p2[i]) return -1;
      if (p1[i] > p2[i]) return 1;
    }
    return 0;
  }
}
#endif

#if defined(__KERNEL__) || defined(XI_NO_STD)
extern "C" {
  void __cxa_pure_virtual() {
    // Handle pure virtual call error
  }
  int __cxa_atexit(void (*)(void *), void *, void *) {
    return 0;
  }
  void* __dso_handle = nullptr;
}
#endif
