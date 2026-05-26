/**
 * @file Routine.cpp
 * @brief Implementation of the coroutine-like task system (Xi Routine).
 */

#include "../../include/Execution/Routine.hpp"

#if defined(ESP32) || defined(ESP_PLATFORM)
#include <freertos/FreeRTOS.h>
#include <freertos/event_groups.h>
#include <freertos/task.h>
#endif

#if !defined(ESP32) && !defined(ESP_PLATFORM)
#include <pthread.h>
#include <time.h>
#endif

namespace Execution {

thread_local RoutineState *current_routine_state = nullptr;

Routine Routine::current() {
  Routine r;
  r.state = current_routine_state;
  return r;
}

StreamBase *Routine::getStreamBase() const {
  if (state && state->task)
    return state->task->getStream();
  return nullptr;
}

void Routine::start() {
  if (!state)
    return;
#if defined(ESP32) || defined(ESP_PLATFORM)
  state->os_cv = (void *)xEventGroupCreate();
  xTaskCreatePinnedToCore(
      [](void *arg) {
        RoutineState *r = (RoutineState *)arg;
        current_routine_state = r;
        if (r->task)
          r->task->run();
        r->is_finished = true;
        if (r->os_cv)
          xEventGroupSetBits((EventGroupHandle_t)r->os_cv, 1);
        vTaskDelete(NULL);
      },
      "XiRoutine", 8192, state, state->priority > 0 ? state->priority : 1,
      (TaskHandle_t *)&state->os_handle,
      (state->threads.size() > 0) ? state->threads[0] : tskNO_AFFINITY);
#else
  state->os_mutex = new pthread_mutex_t;
  state->os_cv = new pthread_cond_t;
  pthread_mutex_init((pthread_mutex_t *)state->os_mutex, NULL);
  pthread_cond_init((pthread_cond_t *)state->os_cv, NULL);

  state->os_handle = new pthread_t;
  pthread_create((pthread_t *)state->os_handle, NULL,
                 [](void *arg) -> void * {
                   RoutineState *r = (RoutineState *)arg;
                   current_routine_state = r;
                   if (r->task)
                     r->task->run();

                   pthread_mutex_lock((pthread_mutex_t *)r->os_mutex);
                   r->is_finished = true;
                   r->is_paused = false;
                   pthread_cond_broadcast((pthread_cond_t *)r->os_cv);
                   pthread_mutex_unlock((pthread_mutex_t *)r->os_mutex);

                   return NULL;
                 },
                 state);
#endif
}

void Routine::pause(u64 delay_ms) {
  if (!state)
    return;
  if (delay_ms > 0) {
    pauseUntil(Xi::millis() + delay_ms);
    return;
  }
#if defined(ESP32) || defined(ESP_PLATFORM)
  ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
#else
  if (!state->os_mutex || !state->os_cv)
    return;
  pthread_mutex_lock((pthread_mutex_t *)state->os_mutex);
  state->is_paused = true;
  while (state->is_paused) {
    pthread_cond_wait((pthread_cond_t *)state->os_cv,
                      (pthread_mutex_t *)state->os_mutex);
  }
  pthread_mutex_unlock((pthread_mutex_t *)state->os_mutex);
#endif
}

void Routine::pauseUntil(u64 time_ms) {
  if (!state)
    return;
  u64 now = Xi::millis();
  if (time_ms <= now)
    return;
  u64 diff = time_ms - now;
#if defined(ESP32) || defined(ESP_PLATFORM)
  ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(diff));
#else
  if (!state->os_mutex || !state->os_cv)
    return;

  struct timespec ts;
#ifdef CLOCK_REALTIME
  clock_gettime(CLOCK_REALTIME, &ts);
  u64 abs_time_ms = ((u64)ts.tv_sec * 1000) + (ts.tv_nsec / 1000000) + diff;
  ts.tv_sec = abs_time_ms / 1000;
  ts.tv_nsec = (abs_time_ms % 1000) * 1000000;
#else
  ts.tv_sec = time_ms / 1000;
  ts.tv_nsec = (time_ms % 1000) * 1000000;
#endif

  pthread_mutex_lock((pthread_mutex_t *)state->os_mutex);
  state->is_paused = true;
  while (state->is_paused) {
    if (pthread_cond_timedwait((pthread_cond_t *)state->os_cv,
                               (pthread_mutex_t *)state->os_mutex, &ts) != 0) {
      break; // Timeout or error
    }
  }
  state->is_paused = false;
  pthread_mutex_unlock((pthread_mutex_t *)state->os_mutex);
#endif
}

void Routine::resume() {
  if (!state)
    return;
#if defined(ESP32) || defined(ESP_PLATFORM)
  if (state->os_handle) {
    xTaskNotifyGive((TaskHandle_t)state->os_handle);
  }
#else
  if (!state->os_mutex || !state->os_cv)
    return;
  pthread_mutex_lock((pthread_mutex_t *)state->os_mutex);
  state->is_paused = false;
  pthread_cond_broadcast((pthread_cond_t *)state->os_cv);
  pthread_mutex_unlock((pthread_mutex_t *)state->os_mutex);
#endif
}

void Routine::await() {
  if (!state)
    return;
#if defined(ESP32) || defined(ESP_PLATFORM)
  if (state->os_cv && !state->is_finished) {
    xEventGroupWaitBits((EventGroupHandle_t)state->os_cv, 1, pdFALSE, pdTRUE,
                        portMAX_DELAY);
  }
#else
  if (state->os_handle) {
    pthread_t tid = *(pthread_t *)state->os_handle;
    if (!pthread_equal(pthread_self(), tid)) {
      pthread_join(tid, NULL);
      delete (pthread_t *)state->os_handle;
      state->os_handle = nullptr;
      state->is_finished = true;
    }
  }
#endif
}

void Routine::destroy() {
  if (!state)
    return;
  await(); // Ensure the task completes before deletion
  if (state->task) {
    delete state->task;
    state->task = nullptr;
  }

#if defined(ESP32) || defined(ESP_PLATFORM)
  if (state->os_cv) {
    vEventGroupDelete((EventGroupHandle_t)state->os_cv);
    state->os_cv = nullptr;
  }
#else
  if (state->os_handle) {
    delete (pthread_t *)state->os_handle;
    state->os_handle = nullptr;
  }
  if (state->os_mutex) {
    pthread_mutex_destroy((pthread_mutex_t *)state->os_mutex);
    delete (pthread_mutex_t *)state->os_mutex;
    state->os_mutex = nullptr;
  }
  if (state->os_cv) {
    pthread_cond_destroy((pthread_cond_t *)state->os_cv);
    delete (pthread_cond_t *)state->os_cv;
    state->os_cv = nullptr;
  }
#endif

  // Remove from static routines list
  for (long long i = routines.size() - 1; i >= 0; --i) {
    if (routines[(usz)i] == state) {
      routines[(usz)i] = routines[routines.size() - 1];
      routines.pop();
      break;
    }
  }

  delete state;
  state = nullptr;
}

usz Routine::size() const {
  if (state && state->task) {
    auto stream_b = state->task->getStream();
    if (stream_b)
      return stream_b->size();
  }
  return 0;
}

void Routine::startScheduler(usz core_id, u32 interrupt_time_ms) {}

void Routine::startSchedulers() {}

} // namespace Execution
