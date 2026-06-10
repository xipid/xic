/**
 * @file Interrupt_Xtensa.cpp
 * @brief ESP32 (Xtensa) timer interrupt implementation for Tasker.
 *
 * Uses ESP-IDF's esp_timer API for periodic tick delivery.
 * The callback invokes Tasker::instance->interrupts() for scheduling.
 */

#if defined(__XTENSA__)

#include "../../../include/Execution/Interrupt.hpp"
#include "../../../include/Execution/Tasker.hpp"

#if defined(ESP_PLATFORM)
#include <esp_timer.h>
#include <esp_cpu.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#endif

namespace Execution {

// -------------------------------------------------------------------------
// Per-Core Timer State
// -------------------------------------------------------------------------

static constexpr usz kMaxCores = 2; // ESP32-S3 is dual-core.

#if defined(ESP_PLATFORM)
static esp_timer_handle_t s_timers[kMaxCores] = {};
#endif
static bool s_timerActive[kMaxCores] = {};

// -------------------------------------------------------------------------
// Timer Callback
// -------------------------------------------------------------------------

#if defined(ESP_PLATFORM)
static void xi_timer_callback(void* arg) {
    if (!Tasker::instance) return;
    usz coreId = reinterpret_cast<usz>(arg);
    Tasker::instance->interrupts(coreId);
}
#endif

// -------------------------------------------------------------------------
// Timer API
// -------------------------------------------------------------------------

void xi_timer_start(usz coreId, u32 intervalUs) {
    if (coreId >= kMaxCores) return;
    if (s_timerActive[coreId]) {
        xi_timer_stop(coreId);
    }

#if defined(ESP_PLATFORM)
    esp_timer_create_args_t args = {};
    args.callback = xi_timer_callback;
    args.arg = reinterpret_cast<void*>(coreId);
    args.dispatch_method = ESP_TIMER_TASK;
    args.name = "xi_tasker";

    if (esp_timer_create(&args, &s_timers[coreId]) != ESP_OK) {
        return;
    }

    if (esp_timer_start_periodic(s_timers[coreId], intervalUs) != ESP_OK) {
        esp_timer_delete(s_timers[coreId]);
        s_timers[coreId] = nullptr;
        return;
    }

    s_timerActive[coreId] = true;
#else
    (void)intervalUs;
#endif
}

void xi_timer_stop(usz coreId) {
    if (coreId >= kMaxCores || !s_timerActive[coreId]) return;

#if defined(ESP_PLATFORM)
    if (s_timers[coreId]) {
        esp_timer_stop(s_timers[coreId]);
        esp_timer_delete(s_timers[coreId]);
        s_timers[coreId] = nullptr;
    }
#endif

    s_timerActive[coreId] = false;
}

usz xi_core_count() {
#if defined(ESP_PLATFORM)
    #if defined(portNUM_PROCESSORS)
        return portNUM_PROCESSORS;
    #else
        return 2; // ESP32-S3 default.
    #endif
#else
    return 1;
#endif
}

usz xi_current_core() {
#if defined(ESP_PLATFORM)
    return static_cast<usz>(esp_cpu_get_core_id());
#else
    return 0;
#endif
}

} // namespace Execution

#endif // defined(__XTENSA__)
