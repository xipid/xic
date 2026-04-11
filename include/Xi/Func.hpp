/**
 * @file Func.hpp
 * @brief Type-erased function wrapper with Small Object Optimization (SBO).

 */

#ifndef XI_CORE_FUNC_HPP
#define XI_CORE_FUNC_HPP

#include "Primitives.hpp"
#include <cstddef>

namespace Xi {

/**
 * @class Func
 * @brief A type-erased function wrapper similar to std::function.
 *
 * Employs Small Object Optimization (SBO) to avoid heap allocations for
 * small callables (up to 128 bytes).
 *
 * @tparam T Function signature (e.g., R(Args...)).
 */
template <typename T> class Func;

/**
 * @brief Specialization for function signatures.
 */
template <typename R, typename... Args> class XI_EXPORT Func<R(Args...)> {
private:
  static constexpr usz SBO_Size = 128; ///< Threshold for SBO.

  struct VTable {
    R (*invoke)(void *, Args...);
    void (*destroy)(void *);
    void (*clone)(const void *, void *);
  };

  union Storage {
    alignas(max_align_t) u8 local[SBO_Size];
    void *heap;
  } data;

  const VTable *vptr;
  bool is_heap;

  // --- Implementation Helpers ---
  template <typename Callable> static R invoke_fn(void *ptr, Args... args) {
    Callable *func = static_cast<Callable *>(ptr);
    return (*func)(args...);
  }

  template <typename Callable> static void destroy_fn(void *ptr) {
    static_cast<Callable *>(ptr)->~Callable();
  }

  template <typename Callable>
  static void clone_fn(const void *src, void *dst) {
    const Callable *source = static_cast<const Callable *>(src);
    if (sizeof(Callable) <= SBO_Size) {
      new (dst) Callable(*source);
    } else {
      Callable *copy = (Callable *)::operator new(sizeof(Callable));
      new (copy) Callable(*source);
      *(void **)dst = (void *)copy;
    }
  }

public:
  /** @brief Constructs an empty (invalid) function. */
  Func() : vptr(nullptr), is_heap(false) { data.heap = nullptr; }

  /** @brief Constructs from a raw function pointer. */
  Func(R (*f)(Args...)) {
    using DecayedF = R (*)(Args...);
    static const VTable vt = {invoke_fn<DecayedF>, destroy_fn<DecayedF>,
                              clone_fn<DecayedF>};
    vptr = &vt;
    new (data.local) DecayedF(f);
    is_heap = false;
  }

  /** @brief Constructs from any callable object (lambdas, functors). */
  template <typename Callable> Func(Callable f) {
    using DecayedF = Callable;
    static const VTable vt = {invoke_fn<DecayedF>, destroy_fn<DecayedF>,
                              clone_fn<DecayedF>};
    vptr = &vt;

    if (sizeof(DecayedF) <= SBO_Size) {
      new (data.local) DecayedF(Xi::Move(f));
      is_heap = false;
    } else {
      DecayedF *heap_ptr = (DecayedF *)::operator new(sizeof(DecayedF));
      new (heap_ptr) DecayedF(Xi::Move(f));
      data.heap = heap_ptr;
      is_heap = true;
    }
  }

  /** @brief Move constructor. */
  Func(Func &&o) noexcept : vptr(o.vptr), is_heap(o.is_heap) {
    if (is_heap) {
      data.heap = o.data.heap;
    } else {
      for (usz i = 0; i < SBO_Size; ++i)
        data.local[i] = o.data.local[i];
    }
    o.vptr = nullptr;
    o.is_heap = false;
    o.data.heap = nullptr;
  }

  /** @brief Copy constructor. */
  Func(const Func &o) : vptr(o.vptr), is_heap(o.is_heap) {
    if (vptr) {
      const void *src = is_heap ? o.data.heap : (const void *)o.data.local;
      vptr->clone(src, (void *)&data);
    }
  }

  /** @brief Assignment operator with copy-and-swap idiom. */
  Func &operator=(Func o) {
    Xi::Swap(vptr, o.vptr);
    Xi::Swap(is_heap, o.is_heap);
    for (usz i = 0; i < SBO_Size; ++i)
      Xi::Swap(data.local[i], o.data.local[i]);
    return *this;
  }

  /** @brief Destructor. */
  ~Func() { _clear(); }

  /** @brief Clears the current function and releases resources. */
  void _clear() {
    if (vptr) {
      void *target = is_heap ? data.heap : (void *)data.local;
      vptr->destroy(target);
      if (is_heap)
        ::operator delete(data.heap);
    }
    vptr = nullptr;
  }

  /** @brief Invokes the wrapped callable. */
  R operator()(Args... args) const {
    if (!vptr)
      return R();
    void *target = is_heap ? data.heap : (void *)data.local;
    return vptr->invoke(target, args...);
  }

  /** @brief Returns true if the function wrapper is valid. */
  bool isValid() const { return vptr != nullptr; }

  /** @brief Explicit boolean conversion for validity check. */
  operator bool() const { return isValid(); }
};

} // namespace Xi

#endif // XI_CORE_FUNC_HPP