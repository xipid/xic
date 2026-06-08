/**
 * @file Tasker.cpp
 * @brief Scheduler core — manages cores, run queues, context switches,
 *        and frequency advisory.
 */

#include "../../include/Execution/Tasker.hpp"

namespace Execution {

// External helpers from Task.cpp
extern void xi_set_current_task(TaskState* s);
extern TaskState* xi_get_current_task();

// -------------------------------------------------------------------------
// Static instance
// -------------------------------------------------------------------------

Tasker* Tasker::instance = nullptr;

// -------------------------------------------------------------------------
// Construction / Destruction
// -------------------------------------------------------------------------

Tasker::Tasker()
    : _nextTaskId(1),
      _schedulePeriodUs(10000),   // 10ms default period.
      _interruptIntervalUs(1000) // 1ms default tick.
{
    if (!instance) {
        instance = this;
    }

    // Create the root task (id=0). The root represents the caller's context.
    TaskState* root = new TaskState();
    root->id = 0;
    root->parentId = 0;
    root->status = TaskStatus::Running;
    root->quotaUs = 0; // Unlimited.
    _tasks.push(root);
}

Tasker::~Tasker() {
    // Disable all cores.
    for (usz i = 0; i < _cores.size(); ++i) {
        if (_cores[i].enabled) {
            xi_timer_stop(_cores[i].id);
            _cores[i].enabled = false;
        }
    }

    // Destroy all tasks.
    for (usz i = 0; i < _tasks.size(); ++i) {
        if (_tasks[i]) {
            TaskState* s = _tasks[i];
            // Free owned memory regions.
            for (usz r = 0; r < s->regions.size(); ++r) {
                if (s->regions[r].owned && s->regions[r].physical) {
                    delete[] s->regions[r].physical;
                }
            }
            // Free AOT cache.
            AOT::destroyCache(s->aotCache);
            // Free stack.
            if (s->stackOwned && s->stack) {
                delete[] s->stack;
            }
            delete s;
        }
    }

    if (instance == this) {
        instance = nullptr;
    }
}

// -------------------------------------------------------------------------
// Core Management
// -------------------------------------------------------------------------

void Tasker::enable(usz coreId) {
    // Expand cores array if needed.
    while (_cores.size() <= coreId) {
        CoreState cs;
        cs.id = _cores.size();
        _cores.push(cs);
    }

    CoreState& core = _cores[coreId];
    if (core.enabled) return;

    core.id = coreId;
    core.enabled = true;
    core.currentTaskId = 0; // Root task.

    xi_timer_start(coreId, _interruptIntervalUs);
}

void Tasker::disable(usz coreId) {
    if (coreId >= _cores.size()) return;
    CoreState& core = _cores[coreId];
    if (!core.enabled) return;

    xi_timer_stop(coreId);

    // Migrate unpinned tasks to other enabled cores.
    for (usz i = 0; i < core.runQueue.size(); ++i) {
        usz taskId = core.runQueue[i];
        Task t = findTask(taskId);
        if (!t.valid()) continue;
        if (!t._state->isPinned) {
            usz newCore = assignCore(taskId);
            if (newCore != coreId && newCore < _cores.size()) {
                _cores[newCore].runQueue.push(taskId);
                t._state->currentCore = newCore;
            }
        }
    }

    core.runQueue.clear();
    core.enabled = false;
    core.currentTaskId = 0;
}

void Tasker::setFrequencySlider(usz coreId, u32 minFreq, u32 maxFreq) {
    while (_cores.size() <= coreId) {
        CoreState cs;
        cs.id = _cores.size();
        _cores.push(cs);
    }
    _cores[coreId].minFreq = minFreq;
    _cores[coreId].maxFreq = maxFreq;
}

// -------------------------------------------------------------------------
// Preemption — the hot path
// -------------------------------------------------------------------------

void Tasker::interrupts(usz coreId) {
    if (coreId >= _cores.size()) return;
    CoreState& core = _cores[coreId];
    if (!core.enabled) return;

    TaskState* current = nullptr;
    if (core.currentTaskId < _tasks.size()) {
        current = _tasks[core.currentTaskId];
    }

    // Wake sleeping tasks.
    i64 now = Xi::micros();
    for (usz i = 0; i < core.runQueue.size(); ++i) {
        usz tid = core.runQueue[i];
        if (tid < _tasks.size() && _tasks[tid]) {
            TaskState* ts = _tasks[tid];
            if (ts->status == TaskStatus::Sleeping &&
                (u64)now >= ts->sleepUntilUs) {
                ts->status = TaskStatus::Ready;
            }
        }
    }

    // Decrement current task's remaining slice.
    if (current && current->status == TaskStatus::Running) {
        if (current->remainingUs > _interruptIntervalUs) {
            current->remainingUs -= _interruptIntervalUs;
            // Still has quota — don't switch.
            return;
        }
        // Quota exhausted or nearly exhausted.
        current->remainingUs = 0;
        current->status = TaskStatus::Ready;
    }

    // Pick next task.
    TaskState* next = pickNext(coreId);

    if (!next) {
        // No runnable tasks. If current was running, let it continue.
        if (current && current->status == TaskStatus::Ready) {
            current->status = TaskStatus::Running;
            resetPeriod(coreId);
        }
        proposeFrequency(coreId);
        return;
    }

    if (next == current) {
        // Same task, reset and continue.
        next->status = TaskStatus::Running;
        if (next->remainingUs == 0) {
            resetPeriod(coreId);
        }
        proposeFrequency(coreId);
        return;
    }

    // Context switch.
    TaskContext* fromCtx = nullptr;
    if (current) {
        fromCtx = &current->context;
    }

    next->status = TaskStatus::Running;
    next->currentCore = coreId;
    core.currentTaskId = next->id;

    xi_set_current_task(next);

    if (next->remainingUs == 0) {
        resetPeriod(coreId);
    }

    proposeFrequency(coreId);

    if (fromCtx) {
        xi_context_switch(fromCtx, &next->context);
    } else {
        // No current task to save. Just restore next.
        // This happens on first scheduling. Save into idle context.
        xi_context_switch(&core.idleContext, &next->context);
    }
}

void Tasker::yield(usz coreId) {
    if (coreId >= _cores.size()) return;
    CoreState& core = _cores[coreId];

    TaskState* current = nullptr;
    if (core.currentTaskId < _tasks.size()) {
        current = _tasks[core.currentTaskId];
    }

    // Pick next.
    TaskState* next = pickNext(coreId);

    if (!next || next == current) {
        if (!next) {
            // No other tasks. Switch to idle context if current is paused/sleeping/finished.
            if (current && current->status != TaskStatus::Running &&
                current->status != TaskStatus::Ready) {
                core.currentTaskId = 0;
                xi_set_current_task(nullptr);
                xi_context_switch(&current->context, &core.idleContext);
            }
        }
        return;
    }

    next->status = TaskStatus::Running;
    next->currentCore = coreId;
    core.currentTaskId = next->id;
    xi_set_current_task(next);

    if (next->remainingUs == 0) {
        resetPeriod(coreId);
    }

    if (current) {
        xi_context_switch(&current->context, &next->context);
    } else {
        xi_context_switch(&core.idleContext, &next->context);
    }
}

// -------------------------------------------------------------------------
// Task Registry
// -------------------------------------------------------------------------

Task Tasker::root() {
    Task t;
    if (_tasks.size() > 0 && _tasks[0]) {
        t._state = _tasks[0];
        t._tasker = this;
    }
    return t;
}

Task Tasker::findTask(usz id) {
    Task t;
    if (id < _tasks.size() && _tasks[id]) {
        t._state = _tasks[id];
        t._tasker = this;
    }
    return t;
}

TaskState* Tasker::allocTask(usz parentId) {
    TaskState* s = new TaskState();
    s->id = _nextTaskId++;
    s->parentId = parentId;
    s->status = TaskStatus::Created;
    s->quotaUs = 0;

    // Expand tasks array to fit.
    while (_tasks.size() <= s->id) {
        _tasks.push(nullptr);
    }
    _tasks[s->id] = s;

    return s;
}

void Tasker::enqueue(usz taskId) {
    if (taskId >= _tasks.size() || !_tasks[taskId]) return;
    TaskState* s = _tasks[taskId];

    usz coreId = 0;
    if (s->isPinned) {
        coreId = s->pinnedCore;
    } else {
        coreId = assignCore(taskId);
    }

    if (coreId >= _cores.size() || !_cores[coreId].enabled) {
        // Find any enabled core.
        for (usz i = 0; i < _cores.size(); ++i) {
            if (_cores[i].enabled) {
                coreId = i;
                break;
            }
        }
    }

    if (coreId >= _cores.size()) return;

    s->currentCore = coreId;

    // Avoid duplicates.
    CoreState& core = _cores[coreId];
    for (usz i = 0; i < core.runQueue.size(); ++i) {
        if (core.runQueue[i] == taskId) return;
    }
    core.runQueue.push(taskId);

    // Update total quota.
    core.totalQuotaUs += s->quotaUs;
}

void Tasker::dequeue(usz taskId) {
    for (usz c = 0; c < _cores.size(); ++c) {
        CoreState& core = _cores[c];
        for (long long i = (long long)core.runQueue.size() - 1; i >= 0; --i) {
            if (core.runQueue[(usz)i] == taskId) {
                // Swap-and-pop.
                usz last = core.runQueue.size() - 1;
                if ((usz)i != last) {
                    core.runQueue[(usz)i] = core.runQueue[last];
                }
                core.runQueue.pop();

                if (taskId < _tasks.size() && _tasks[taskId]) {
                    core.totalQuotaUs -= _tasks[taskId]->quotaUs;
                }
                return;
            }
        }
    }
}

void Tasker::destroyTask(usz taskId) {
    if (taskId >= _tasks.size() || !_tasks[taskId]) return;
    TaskState* s = _tasks[taskId];

    // Recursively destroy children first.
    // Copy child IDs since the array will be modified.
    Array<usz> childIds = s->childIds;
    for (usz i = 0; i < childIds.size(); ++i) {
        destroyTask(childIds[i]);
    }

    // Dequeue.
    dequeue(taskId);

    // Remove from parent's child list.
    if (s->parentId < _tasks.size() && _tasks[s->parentId]) {
        TaskState* parent = _tasks[s->parentId];
        for (long long i = (long long)parent->childIds.size() - 1; i >= 0; --i) {
            if (parent->childIds[(usz)i] == taskId) {
                usz last = parent->childIds.size() - 1;
                if ((usz)i != last) {
                    parent->childIds[(usz)i] = parent->childIds[last];
                }
                parent->childIds.pop();
                break;
            }
        }
    }

    // Free owned memory regions.
    for (usz i = 0; i < s->regions.size(); ++i) {
        if (s->regions[i].owned && s->regions[i].physical) {
            delete[] s->regions[i].physical;
        }
    }

    // Free AOT cache.
    AOT::destroyCache(s->aotCache);

    // Free stack.
    if (s->stackOwned && s->stack) {
        delete[] s->stack;
    }

    s->status = TaskStatus::Destroyed;
    delete s;
    _tasks[taskId] = nullptr;
}

// -------------------------------------------------------------------------
// Internal: Core State Access
// -------------------------------------------------------------------------

CoreState* Tasker::coreState(usz coreId) {
    if (coreId >= _cores.size()) return nullptr;
    return &_cores[coreId];
}

TaskState* Tasker::currentTask(usz coreId) {
    if (coreId >= _cores.size()) return nullptr;
    usz tid = _cores[coreId].currentTaskId;
    if (tid >= _tasks.size()) return nullptr;
    return _tasks[tid];
}

// -------------------------------------------------------------------------
// Internal: Scheduler
// -------------------------------------------------------------------------

TaskState* Tasker::pickNext(usz coreId) {
    if (coreId >= _cores.size()) return nullptr;
    CoreState& core = _cores[coreId];

    if (core.runQueue.size() == 0) return nullptr;

    // Weighted round-robin: find the next Ready task with the highest
    // effective priority. Tasks with quotaUs == 0 (unlimited) get
    // lowest priority when the core is under pressure.
    usz bestIdx = core.runQueue.size(); // Invalid sentinel.
    u64 bestWeight = 0;
    bool anyReady = false;

    usz startIdx = core.runQueueIndex % core.runQueue.size();
    usz count = core.runQueue.size();

    for (usz n = 0; n < count; ++n) {
        usz idx = (startIdx + n) % count;
        usz tid = core.runQueue[idx];
        if (tid >= _tasks.size() || !_tasks[tid]) continue;

        TaskState* ts = _tasks[tid];

        if (ts->status != TaskStatus::Ready) continue;

        anyReady = true;

        // Weight: quota > 0 means explicit priority.
        // quota == 0 (unlimited/background) gets weight 1.
        u64 weight = ts->quotaUs > 0 ? ts->quotaUs : 1;

        if (bestIdx >= core.runQueue.size() || weight > bestWeight) {
            bestWeight = weight;
            bestIdx = idx;
        }
    }

    if (!anyReady) return nullptr;

    // Advance round-robin index past the selected task.
    core.runQueueIndex = (bestIdx + 1) % core.runQueue.size();

    usz selectedId = core.runQueue[bestIdx];
    if (selectedId < _tasks.size()) {
        return _tasks[selectedId];
    }
    return nullptr;
}

usz Tasker::assignCore(usz taskId) {
    // Pick the enabled core with the lowest total quota (least loaded).
    usz bestCore = 0;
    u64 bestLoad = (u64)-1;
    bool found = false;

    for (usz i = 0; i < _cores.size(); ++i) {
        if (!_cores[i].enabled) continue;
        if (!found || _cores[i].totalQuotaUs < bestLoad) {
            bestCore = i;
            bestLoad = _cores[i].totalQuotaUs;
            found = true;
        }
    }

    (void)taskId;
    return bestCore;
}

void Tasker::resetPeriod(usz coreId) {
    if (coreId >= _cores.size()) return;
    CoreState& core = _cores[coreId];

    for (usz i = 0; i < core.runQueue.size(); ++i) {
        usz tid = core.runQueue[i];
        if (tid >= _tasks.size() || !_tasks[tid]) continue;
        TaskState* ts = _tasks[tid];

        if (ts->quotaUs > 0) {
            ts->remainingUs = ts->quotaUs;
        } else {
            // Unlimited: give the full scheduling period.
            ts->remainingUs = _schedulePeriodUs;
        }
    }
}

void Tasker::proposeFrequency(usz coreId) {
    if (coreId >= _cores.size()) return;
    CoreState& core = _cores[coreId];

    if (core.maxFreq == 0 || !onChangeFrequency) return;

    // Compute pressure: ratio of demanded quota to available time.
    u64 totalDemand = 0;
    usz readyCount = 0;

    for (usz i = 0; i < core.runQueue.size(); ++i) {
        usz tid = core.runQueue[i];
        if (tid >= _tasks.size() || !_tasks[tid]) continue;
        TaskState* ts = _tasks[tid];
        if (ts->status == TaskStatus::Ready || ts->status == TaskStatus::Running) {
            totalDemand += ts->quotaUs > 0 ? ts->quotaUs : _schedulePeriodUs;
            readyCount++;
        }
    }

    u32 proposed;
    if (readyCount == 0) {
        // Idle — propose minimum.
        proposed = core.minFreq;
    } else if (totalDemand >= _schedulePeriodUs) {
        // Tight — propose maximum.
        proposed = core.maxFreq;
    } else {
        // Relaxed — interpolate.
        u64 range = core.maxFreq - core.minFreq;
        u64 ratio = (totalDemand * range) / _schedulePeriodUs;
        proposed = core.minFreq + (u32)ratio;
    }

    if (proposed != core.currentProposedFreq) {
        core.currentProposedFreq = proposed;
        onChangeFrequency(coreId, proposed);
    }
}

} // namespace Execution
