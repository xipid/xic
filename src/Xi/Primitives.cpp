#include <Xi/Primitives.hpp>
#include <Xi/String.hpp>

#if defined(ESP_PLATFORM)
#include "esp_timer.h"
#elif defined(_WIN32)
#include <windows.h>
#else
#include <time.h>
#endif

namespace Xi {

i64 millis() {
#if defined(ARDUINO)
    return ::millis();
#elif defined(ESP_PLATFORM)
    return esp_timer_get_time() / 1000ULL;
#elif defined(_WIN32)
    return ::GetTickCount();
#else
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (i64)(ts.tv_sec * 1000 + ts.tv_nsec / 1000000);
#endif
}

i64 micros() {
#if defined(ARDUINO)
    return ::micros();
#elif defined(ESP_PLATFORM)
    return esp_timer_get_time();
#elif defined(_WIN32)
    static long long freq = 0;
    if (freq == 0)
        ::QueryPerformanceFrequency((LARGE_INTEGER*)&freq);
    long long counter;
    ::QueryPerformanceCounter((LARGE_INTEGER*)&counter);
    return (i64)(counter * 1000000 / freq);
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

// FNVHasher specializations are usually templates and stay in header 
// unless we want to move specific ones. Specializations for POD types
// can be moved to .cpp if they are not templates anymore.
// But FNVHasher is a template struct.

} // namespace Xi
