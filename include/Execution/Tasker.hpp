/**
 * @file Tasker.hpp
 * @brief Top-level scheduler that manages cores, tasks, and preemption.
 *
 * Tasker is the kernel of the system. It owns all TaskState objects,
 * drives the run queues, handles context switches on timer interrupts,
 * and proposes CPU frequency changes based on load.
 */

#ifndef XI_EXECUTION_TASKER_HPP
#define XI_EXECUTION_TASKER_HPP

#include "../Collection/Array.hpp"
#include "../Xi/Func.hpp"
#include "Interrupt.hpp"
#include "Task.hpp"

namespace Execution {

using namespace Xi;
using namespace Collection;

// -------------------------------------------------------------------------
// Core State
// -------------------------------------------------------------------------

/**
 * @struct CoreState
 * @brief Per-core scheduling state.
 */
struct CoreState {
    usz id;                     ///< Core ID.
    bool enabled;               ///< Whether this core is managed by Tasker.
    u32 minFreq;                ///< Minimum proposed frequency (Hz). 0 = no control.
    u32 maxFreq;                ///< Maximum proposed frequency (Hz). 0 = no control.
    u32 currentProposedFreq;    ///< Last proposed frequency.
    Array<usz> runQueue;        ///< Task IDs in the run queue for this core.
    usz currentTaskId;          ///< ID of the currently executing task (0 = idle).
    usz runQueueIndex;          ///< Current position in round-robin.
    u64 totalQuotaUs;           ///< Sum of quotas of all tasks on this core.
    TaskContext idleContext;    ///< Context to return to when no tasks are ready.

    CoreState()
        : id(0), enabled(false), minFreq(0), maxFreq(0),
          currentProposedFreq(0), currentTaskId(0), runQueueIndex(0),
          totalQuotaUs(0), idleContext{} {}
};

// -------------------------------------------------------------------------
// Tasker
// -------------------------------------------------------------------------

/**
 * @class Tasker
 * @brief Manages scheduling across one or more CPU cores.
 *
 * Usage:
 * @code
 *   Tasker tasker;
 *   tasker.enable(0);  // Enable core 0
 *   tasker.setFrequencySlider(0, 80000000, 240000000);
 *   tasker.onChangeFrequency = [](usz core, u32 freq) {
 *       // User sets hardware frequency here or ignores it.
 *   };
 *
 *   Task root = tasker.root();
 *   Task child = root.spawn();
 *   child.setEntry(myFunction, myArg);
 *   child.setQuota(1000); // 1ms per period
 *   child.start();
 * @endcode
 */
class XI_EXPORT Tasker {
public:
    Tasker();
    ~Tasker();

    // -- Core Management --

    /**
     * @brief Enables a core for task scheduling.
     *
     * Starts the timer interrupt on this core. Tasks can be assigned to it.
     *
     * @param coreId  The hardware core ID.
     */
    void enable(usz coreId);

    /**
     * @brief Disables a core.
     *
     * Saves all register state, migrates unpinned tasks to other cores,
     * and stops the timer interrupt.
     *
     * @param coreId  The core to disable.
     */
    void disable(usz coreId);

    /**
     * @brief The preemption entry point — called by the timer ISR.
     *
     * This is the hot path. On each tick:
     *   1. Decrements the current task's remaining quota slice.
     *   2. If exhausted or task yielded, saves context and picks next.
     *   3. Computes frequency pressure and proposes changes.
     *   4. Loads the next task's context.
     *
     * MUST be called on the target core (from ISR or signal handler).
     *
     * @param coreId  The core that received the interrupt.
     */
    void interrupts(usz coreId);

    /**
     * @brief Sets the frequency slider for a core.
     *
     * Tasker uses these bounds to compute proposed frequencies based on
     * load pressure. Setting maxFreq to 0 disables frequency proposals.
     *
     * @param coreId   Core ID.
     * @param minFreq  Minimum frequency in Hz.
     * @param maxFreq  Maximum frequency in Hz (0 = don't control).
     */
    void setFrequencySlider(usz coreId, u32 minFreq, u32 maxFreq);

    /**
     * @brief Callback invoked when Tasker proposes a frequency change.
     *
     * The user hooks this to hardware DVFS (e.g. esp_pm_configure),
     * or ignores it entirely. Tasker never changes frequency itself.
     */
    Func<void(usz coreId, u32 proposedFreq)> onChangeFrequency;

    // -- Task Registry --

    /**
     * @brief Returns a handle to the root task.
     *
     * The root task is automatically created when the Tasker is constructed.
     * It has no parent and serves as the ancestor of all other tasks.
     */
    Task root();

    /**
     * @brief Finds a task by ID.
     *
     * @param id  The task ID.
     * @return Task handle, or an invalid Task if not found.
     */
    Task findTask(usz id);

    /**
     * @brief Returns the number of registered tasks.
     */
    usz taskCount() const { return _tasks.size(); }

    // -- Scheduling Configuration --

    /**
     * @brief Sets the scheduling period length in microseconds.
     *
     * Task quotas are enforced per period. Default is 10000 (10ms).
     *
     * @param periodUs  Period length in microseconds.
     */
    void setSchedulePeriod(u64 periodUs) { _schedulePeriodUs = periodUs; }

    /**
     * @brief Sets the timer interrupt interval in microseconds.
     *
     * Finer intervals give more responsive preemption but higher overhead.
     * Default is 1000 (1ms).
     *
     * @param intervalUs  Interrupt interval in microseconds.
     */
    void setInterruptInterval(u32 intervalUs) { _interruptIntervalUs = intervalUs; }

    // -- Internal (used by Task) --

    /** @brief Allocates a new TaskState and registers it. */
    TaskState* allocTask(usz parentId);

    /** @brief Enqueues a task on the appropriate core's run queue. */
    void enqueue(usz taskId);

    /** @brief Removes a task from all run queues. */
    void dequeue(usz taskId);

    /** @brief Destroys a task and its subtree. */
    void destroyTask(usz taskId);

    /** @brief Yields the current task on the given core. */
    void yield(usz coreId);

    /** @brief Returns the CoreState for a core, or nullptr. */
    CoreState* coreState(usz coreId);

    /** @brief Returns the current task on a given core. */
    TaskState* currentTask(usz coreId);

    /** @brief The global Tasker instance (set by the first Tasker constructed). */
    static Tasker* instance;

private:
    Array<TaskState*> _tasks;       ///< All registered tasks, indexed by ID.
    Array<CoreState> _cores;        ///< Per-core state.
    usz _nextTaskId;                ///< Counter for task ID assignment.
    u64 _schedulePeriodUs;          ///< Scheduling period length.
    u32 _interruptIntervalUs;       ///< Timer interrupt interval.

    /** @brief Picks the next runnable task on a core. */
    TaskState* pickNext(usz coreId);

    /** @brief Computes and proposes a frequency based on core load. */
    void proposeFrequency(usz coreId);

    /** @brief Assigns a task to the least-loaded enabled core. */
    usz assignCore(usz taskId);

    /** @brief Resets all task quota slices for a new scheduling period. */
    void resetPeriod(usz coreId);
};

} // namespace Execution

#endif // XI_EXECUTION_TASKER_HPP
