/**
 * @file Interrupt_Amd64.cpp
 * @brief Linux x86_64 timer interrupt implementation using POSIX timers.
 *
 * Uses timer_create(CLOCK_MONOTONIC) with SIGALRM to drive periodic
 * preemption ticks. The signal handler invokes Task::yield()
 * to perform context switching and scheduling decisions.
 *
 * Platform functions:
 *   - xi_timer_start:   Creates and arms a per-core POSIX timer.
 *   - xi_timer_stop:    Disarms and deletes the timer.
 *   - xi_core_count:    Returns online CPU count via sysconf.
 *   - xi_current_core:  Returns executing CPU via sched_getcpu.
 */

#if defined(__x86_64__) || defined(_M_X64)

#include "../../../include/Task/Interrupt.hpp"
#include "../../../include/Task/Task.hpp"

#include <csignal>
#include <cstring>
#include <ctime>
#include <sched.h>
#include <unistd.h>

namespace Task {

// -------------------------------------------------------------------------
// Per-Core Timer State
// -------------------------------------------------------------------------

/// Maximum number of cores we track timers for.
static constexpr usz kMaxCores = 64;

/// POSIX timer handles, one per core.
static timer_t s_timers[kMaxCores] = {};

/// Whether each core's timer is currently active.
static bool s_timerActive[kMaxCores] = {};

// -------------------------------------------------------------------------
// Signal Handler
// -------------------------------------------------------------------------

/**
 * @brief SIGALRM handler that dispatches to Task::yield.
 *
 * The signal is delivered with si_value.sival_int set to the core ID
 * that created the timer. We forward this to the Task instance
 * for scheduling decisions.
 *
 * Note: On Linux, POSIX timer signals are delivered to the thread that
 * created the timer, which should be the thread bound to that core.
 * If the timer was created from a different thread, sched_getcpu()
 * provides the actual executing core as a fallback.
 */
static void xi_timer_signal_handler(int sig, siginfo_t* info, void* /* ucontext */) {
    (void)sig;

    if (!Task::instance) {
        return;
    }

    // Retrieve the core ID from the signal payload.
    usz coreId = 0;
    if (info != nullptr) {
        coreId = static_cast<usz>(info->si_value.sival_int);
    }

    Task::current().yield(coreId);
}

// -------------------------------------------------------------------------
// Timer API Implementation
// -------------------------------------------------------------------------

void xi_timer_start(usz coreId, u32 intervalUs) {
    if (coreId >= kMaxCores) {
        return;
    }

    // If a timer is already active on this core, stop it first.
    if (s_timerActive[coreId]) {
        xi_timer_stop(coreId);
    }

    // Install the signal handler (once, idempotent via sa_flags).
    // We use SIGRTMIN + coreId % 32 to avoid clashing with SIGALRM
    // if multiple cores need independent timers. But for simplicity
    // on hosted Linux (where threads handle their own signals), we
    // use a single signal: SIGALRM.
    struct sigaction sa;
    std::memset(&sa, 0, sizeof(sa));
    sa.sa_sigaction = xi_timer_signal_handler;
    sa.sa_flags = SA_SIGINFO | SA_RESTART;
    sigemptyset(&sa.sa_mask);

    if (sigaction(SIGALRM, &sa, nullptr) != 0) {
        return; // Signal handler installation failed.
    }

    // Create the POSIX timer.
    struct sigevent sev;
    std::memset(&sev, 0, sizeof(sev));
    sev.sigev_notify = SIGEV_SIGNAL;
    sev.sigev_signo = SIGALRM;
    sev.sigev_value.sival_int = static_cast<int>(coreId);

    timer_t timerId;
    if (timer_create(CLOCK_MONOTONIC, &sev, &timerId) != 0) {
        return; // Timer creation failed.
    }

    s_timers[coreId] = timerId;

    // Arm the timer with the specified interval.
    struct itimerspec its;
    its.it_value.tv_sec = static_cast<time_t>(intervalUs / 1000000);
    its.it_value.tv_nsec = static_cast<long>((intervalUs % 1000000) * 1000);
    its.it_interval.tv_sec = its.it_value.tv_sec;
    its.it_interval.tv_nsec = its.it_value.tv_nsec;

    // Ensure we don't set a zero initial expiration (would disarm the timer).
    if (its.it_value.tv_sec == 0 && its.it_value.tv_nsec == 0) {
        its.it_value.tv_nsec = 1000; // Minimum 1µs.
    }

    if (timer_settime(timerId, 0, &its, nullptr) != 0) {
        timer_delete(timerId);
        return;
    }

    s_timerActive[coreId] = true;
}

void xi_timer_stop(usz coreId) {
    if (coreId >= kMaxCores || !s_timerActive[coreId]) {
        return;
    }

    // Disarm the timer by setting interval to zero.
    struct itimerspec its;
    std::memset(&its, 0, sizeof(its));
    timer_settime(s_timers[coreId], 0, &its, nullptr);

    // Delete the timer.
    timer_delete(s_timers[coreId]);
    s_timers[coreId] = nullptr;
    s_timerActive[coreId] = false;
}

usz xi_core_count() {
    long count = sysconf(_SC_NPROCESSORS_ONLN);
    if (count <= 0) {
        return 1; // Fallback: at least one core.
    }
    return static_cast<usz>(count);
}

usz xi_current_core() {
    int cpu = sched_getcpu();
    if (cpu < 0) {
        return 0; // Fallback: assume core 0.
    }
    return static_cast<usz>(cpu);
}

} // namespace Task

#endif // defined(__x86_64__) || defined(_M_X64)
