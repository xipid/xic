/**
 * @file Interrupt.hpp
 * @brief Portable timer interrupt interface for preemptive task scheduling.
 *
 * Each architecture backend implements these functions to set up
 * a periodic hardware timer that calls Task::yield(coreId)
 * on each tick.
 */

#ifndef XI_EXECUTION_INTERRUPT_HPP
#define XI_EXECUTION_INTERRUPT_HPP

#include "../Xi/Primitives.hpp"

namespace Execution {

using namespace Xi;

/**
 * @brief Starts a periodic timer interrupt on the given core.
 *
 * When the timer fires, it must call Task::yield(coreId)
 * in ISR context (or signal context on hosted platforms).
 *
 * @param coreId     The core to attach the timer to.
 * @param intervalUs Interval between ticks in microseconds.
 */
void xi_timer_start(usz coreId, u32 intervalUs);

/**
 * @brief Stops the periodic timer on the given core.
 *
 * @param coreId The core whose timer should be stopped.
 */
void xi_timer_stop(usz coreId);

/**
 * @brief Returns the number of available CPU cores on this platform.
 */
usz xi_core_count();

/**
 * @brief Returns the ID of the core executing this call.
 */
usz xi_current_core();

} // namespace Execution

#endif // XI_EXECUTION_INTERRUPT_HPP
