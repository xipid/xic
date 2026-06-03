/**
 * @file Random.cpp
 * @brief Implementation of random number generation utilities.
 */

#include "../../include/Xi/Random.hpp"
#include "../../include/Security/Crypto.hpp"
#if !defined(__KERNEL__) && !defined(XI_NO_STD)
#include <fcntl.h>
#include <sys/time.h>
#include <unistd.h>
#if __has_include(<sys/mman.h>) && defined(__linux__)
#include <sys/mman.h>
#endif
#endif

#if defined(__KERNEL__)
#include <linux/random.h>
#endif

#if !defined(__KERNEL__)
#include <string.h>
#endif

#if defined(_WIN32)
extern "C" __declspec(dllimport) unsigned char __stdcall SystemFunction036(void* RandomBuffer, unsigned long RandomBufferLength);
#endif

namespace Xi {

alignas(64) u32 _randomPool[20] = {
    123456789, 362436069, 521288629, 88675123, 0, 0, 0, 0, 0, 0,
    0,         0,         0,         0,        0, 0, 0, 0, 0, 0};
bool _randomInitialized = false;

u32 randomNext() {
  u32 t = _randomPool[3];
  u32 s = _randomPool[0];
  _randomPool[3] = _randomPool[2];
  _randomPool[2] = _randomPool[1];
  _randomPool[1] = s;
  t ^= t << 11;
  t ^= t >> 8;
  _randomPool[0] = t ^ s ^ (s >> 19);
  return _randomPool[0];
}

void randomSeed(u32 s, bool overwrite) {
  if (!overwrite && _randomInitialized)
    return;
  for (int i = 0; i < 20; i++) {
    s = 1812433253U * (s ^ (s >> 30)) + i;
    _randomPool[i] = s;
  }
  for (int i = 0; i < 10; i++)
    randomNext();
  _randomInitialized = true;
}

void randomSeed(bool overwrite) {
  if (!overwrite && _randomInitialized)
    return;
#if defined(__KERNEL__)
  get_random_bytes(_randomPool, sizeof(_randomPool));
  _randomInitialized = true;
#elif defined(ESP_PLATFORM)
  for (int i = 0; i < 20; i++)
    _randomPool[i] = esp_random();
  _randomInitialized = true;
#elif defined(_WIN32)
  if (SystemFunction036(_randomPool, sizeof(_randomPool))) {
    _randomInitialized = true;
  }
#elif defined(ARDUINO)
  u32 seed = 0;
  for (int i = 0; i < 16; i++) {
    seed = (seed << 2) | (analogRead(0) & 3);
  }
  randomSeed(seed, overwrite);
#elif defined(XI_NO_STD) && defined(__linux__)
  #if defined(__x86_64__)
    long ret;
    __asm__ volatile (
      "syscall"
      : "=a"(ret)
      : "a"(318), "D"(_randomPool), "S"(sizeof(_randomPool)), "d"(0)
      : "rcx", "r11", "memory"
    );
    if (ret > 0) {
      _randomInitialized = true;
    }
  #else
    randomSeed((u32)987654321, overwrite);
  #endif
#elif defined(__linux__) || defined(__APPLE__)
  int fd = open("/dev/urandom", O_RDONLY);
  if (fd >= 0) {
    ssize_t n = read(fd, _randomPool, sizeof(_randomPool));
    (void)n;
    close(fd);
    _randomInitialized = true;
  }
#else
  randomSeed((u32)987654321, overwrite);
#endif

#if defined(__linux__)
  #if defined(XI_NO_STD)
    #if defined(__x86_64__)
      long ret;
      __asm__ volatile (
        "syscall"
        : "=a"(ret)
        : "a"(28), "D"(_randomPool), "S"(sizeof(_randomPool)), "d"(18)
        : "rcx", "r11", "memory"
      );
    #endif
  #elif __has_include(<sys/mman.h>)
    madvise(_randomPool, sizeof(_randomPool), MADV_WIPEONFORK);
  #endif
#endif
}

u32 random(u32 max) {
  if (max == 0)
    return 0;
  return randomNext() % max;
}

i32 random(i32 min, i32 max) {
  if (min >= max)
    return min;
  return min + (i32)(randomNext() % (u32)(max - min));
}

f32 randomFloat() { return (f32)randomNext() / 4294967295.0f; }

void randomFill(u8 *buffer, usz size) {
  usz i = 0;
  while (i + 4 <= size) {
    u32 r = randomNext();
    memcpy(buffer + i, &r, 4);
    i += 4;
  }
  if (i < size) {
    u32 r = randomNext();
    while (i < size) {
      buffer[i++] = (u8)(r & 0xFF);
      r >>= 8;
    }
  }
}

/**
 * @brief Fills a string with pseudo-random bytes.
 */
void randomFill(String &s, usz len) {
  if (len == 0 || s.size() < len)
    len = s.size();
  u8 *raw = const_cast<u8 *>(reinterpret_cast<const u8 *>(s.data()));
  if (raw)
    randomFill(raw, len);
}

/**
 * @brief Utility for seeding the PRNG using string hash.
 */
inline void randomSeed(String str) {
  u32 h = 5381;
  int c;
  unsigned char *d = (unsigned char *)str.data();
  while (d && (c = *d++)) {
    h = ((h << 5) + h) + c;
  }
  randomSeed(h);
}

} // namespace Xi
