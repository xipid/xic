#include "../../include/Xi/Random.hpp"
#include <fcntl.h>
#include <string.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

#if __has_include(<sys/mman.h>) && defined(__linux__)
#include <sys/mman.h>
#endif

namespace Xi {

alignas(64) u32 _randomPool[20] = {
    123456789, 362436069, 521288629, 88675123, 0, 0, 0, 0, 0, 0,
    0,         0,         0,         0,        0, 0, 0, 0, 0, 0};
bool _randomInitialized = false;
u32 _secureCounter = 0;

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

void randomSeed(u32 s) {
  for (int i = 0; i < 20; i++) {
    s = 1812433253U * (s ^ (s >> 30)) + i;
    _randomPool[i] = s;
  }
  for (int i = 0; i < 10; i++)
    randomNext();
  _randomInitialized = true;
}

void randomSeed() {
#if defined(__linux__) || defined(__APPLE__)
  int fd = open("/dev/urandom", O_RDONLY);
  if (fd >= 0) {
    read(fd, _randomPool, sizeof(_randomPool));
    close(fd);
    _randomInitialized = true;
  }
#elif defined(ESP_PLATFORM)
  for (int i = 0; i < 20; i++)
    _randomPool[i] = esp_random();
#else
  randomSeed(987654321);
#endif

#if defined(__linux__) && __has_include(<sys/mman.h>)
  madvise(_randomPool, sizeof(_randomPool), MADV_WIPEONFORK);
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

} // namespace Xi
