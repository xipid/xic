/**
 * @file Interrupt_RiscV32.cpp
 * @brief RISC-V32 (ESP32-C3/C6) timer interrupt implementation for Tasker.
 *
 * Uses ESP-IDF's esp_timer API. ESP32-C3 is single-core RISC-V,
 * ESP32-C6 is single-core RISC-V (with LP core, not managed here).
 */

#if defined(__riscv) && (__riscv_xlen == 32)

#include "../../../include/Execution/Interrupt.hpp"
#include "../../../include/Execution/Tasker.hpp"

#if defined(ESP_PLATFORM)
#include <esp_timer.h>
#include <esp_cpu.h>
#endif

namespace Execution {

// -------------------------------------------------------------------------
// Timer State (single core)
// -------------------------------------------------------------------------

#if defined(ESP_PLATFORM)
static esp_timer_handle_t s_timer = nullptr;
#endif
static bool s_timerActive = false;

// -------------------------------------------------------------------------
// Timer Callback
// -------------------------------------------------------------------------

#if defined(ESP_PLATFORM)
static void xi_timer_callback(void* /* arg */) {
    if (!Tasker::instance) return;
    Tasker::instance->interrupts(0);
}
#endif

// -------------------------------------------------------------------------
// Timer API
// -------------------------------------------------------------------------

void xi_timer_start(usz coreId, u32 intervalUs) {
    (void)coreId; // Single core — always core 0.
    if (s_timerActive) {
        xi_timer_stop(0);
    }

#if defined(ESP_PLATFORM)
    esp_timer_create_args_t args = {};
    args.callback = xi_timer_callback;
    args.arg = nullptr;
    args.dispatch_method = ESP_TIMER_TASK;
    args.name = "xi_tasker";

    if (esp_timer_create(&args, &s_timer) != ESP_OK) {
        return;
    }

    if (esp_timer_start_periodic(s_timer, intervalUs) != ESP_OK) {
        esp_timer_delete(s_timer);
        s_timer = nullptr;
        return;
    }

    s_timerActive = true;
#else
    (void)intervalUs;
#endif
}

void xi_timer_stop(usz coreId) {
    (void)coreId;
    if (!s_timerActive) return;

#if defined(ESP_PLATFORM)
    if (s_timer) {
        esp_timer_stop(s_timer);
        esp_timer_delete(s_timer);
        s_timer = nullptr;
    }
#endif

    s_timerActive = false;
}

usz xi_core_count() {
    return 1; // ESP32-C3/C6 are single-core RISC-V.
}

usz xi_current_core() {
    return 0; // Always core 0.
}

} // namespace Execution

#endif // defined(__riscv) && (__riscv_xlen == 32)
