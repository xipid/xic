/**
 * @file Routine.hpp
 * @brief Coroutine and task scheduling system for the Xi framework.

 */

#ifndef XI_CORE_ROUTINE_HPP
#define XI_CORE_ROUTINE_HPP

#include "../Collection/Stream.hpp"
#include "../Collection/String.hpp"
#include "../Xi/Func.hpp"

using namespace Xi;
using namespace Collection;

namespace Execution {

class Routine;

/**
 * @class RoutineTaskBase
 * @brief Base class for type-erased tasks executed within a Routine.
 */
class RoutineTaskBase {
public:
  virtual ~RoutineTaskBase() = default;
  virtual void run() = 0;
  virtual StreamBase *getStream() = 0;
};

/**
 * @class StreamProxy
 * @brief Internal proxy for type-erased stream operations.
 */
template <typename YieldT, typename InverseT> class StreamProxy {
  StreamBase *base_ptr;

public:
  StreamProxy(StreamBase *ptr) : base_ptr(ptr) {}

  void push(const YieldT &val) {
    if (!base_ptr)
      return;
    auto *typed = static_cast<Stream<YieldT, InverseT> *>(base_ptr);
    typed->push(val);
  }

  InverseT iunshift() {
    if (!base_ptr)
      return InverseT();
    auto *typed = static_cast<Stream<YieldT, InverseT> *>(base_ptr);
    return typed->ishift();
  }
};

/**
 * @struct RoutineState
 * @brief Internal state and metadata for a running Routine.
 */
struct RoutineState {
  RoutineTaskBase *task;
  usz id;
  InlineArray<usz> threads; ///< Affinity mask/cores to run on.
  usz priority;
  void *os_handle;
  void *os_mutex;
  void *os_cv;
  bool is_paused;
  bool is_finished;

  RoutineState()
      : task(nullptr), id(0), priority(0), os_handle(nullptr),
        os_mutex(nullptr), os_cv(nullptr), is_paused(false),
        is_finished(false) {}
};

/**
 * @class Routine
 * @brief Handle to a scheduled task or coroutine.
 */
class XI_EXPORT Routine {
public:
  RoutineState *state;
  inline static InlineArray<RoutineState *> routines;

  Routine() : state(nullptr) {}
  ~Routine() = default;

  /** @brief Waits for the routine to complete. */
  void await();
  /** @brief Pauses the routine until a specific timestamp. */
  void pauseUntil(u64 time_ms);
  /** @brief Pauses the routine for a delay (0 for indefinite). */
  void pause(u64 delay_ms = 0);
  /** @brief Resumes a paused routine. */
  void resume();
  /** @brief Force-destroys the routine. */
  void destroy();

  /** @brief Returns number of yielded elements in the output stream. */
  usz size() const;
  /** @brief Returns the base stream associated with this routine. */
  StreamBase *getStreamBase() const;

  /** @brief Returns a handle to the currently executing routine. */
  static Routine current();
  /** @brief Primary entry point for the scheduler on a specific core. */
  static void startScheduler(usz core_id, u32 interrupt_time_ms = 1);
  /** @brief Automatically starts schedulers on all available cores. */
  static void startSchedulers();

  /**
   * @brief Creates and starts a new routine from a callable.
   */
  template <typename Fn, typename... Args>
  static Routine run(Fn fn, Args... args) {
    using Ret = decltype(fn(args...));

    class TaskImpl : public RoutineTaskBase {
    public:
      Xi::Func<Ret()> m_bound;
      Ret m_stream;

      TaskImpl(Xi::Func<Ret()> bound) : m_bound(Xi::Move(bound)), m_stream() {}

      void run() override {
        Ret result_stream = m_bound();
        for (usz i = 0; i < result_stream.size(); ++i)
          m_stream.push(result_stream[i]);
      }

      StreamBase *getStream() override {
        return static_cast<StreamBase *>(&m_stream);
      }
    };

    auto bound_fn = [fn, args...]() mutable -> Ret { return fn(args...); };

    Routine rtn;
    rtn.state = new RoutineState();
    rtn.state->task = new TaskImpl(bound_fn);
    routines.push(rtn.state);
    rtn.start();
    return rtn;
  }

private:
  void start();
};

// ----- Global Pseudo-Methods -----

/** @brief Global helper to push data to the current routine's stream. */
inline void push(const String &val) {
  auto r = Routine::current();
  if (r.state) {
    StreamProxy<String, String>(r.getStreamBase()).push(val);
    r.pause();
  }
}

/** @brief Global helper to read from the current routine's inverse stream. */
inline String iunshift() {
  auto r = Routine::current();
  return r.state ? StreamProxy<String, String>(r.getStreamBase()).iunshift()
                 : String();
}

/** @brief Returns size of the inverse stream of the current routine. */
inline usz isize() {
  auto r = Routine::current();
  return r.state ? r.getStreamBase()->isize() : 0;
}

/** @brief Returns size of the primary yield stream of the current routine. */
inline usz size() {
  auto r = Routine::current();
  return r.state ? r.getStreamBase()->size() : 0;
}

} // namespace Execution

#endif // XI_CORE_ROUTINE_HPP
