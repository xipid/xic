/**
 * @file Func.hpp
 * @brief Zero-VTable type-erased function wrapper with SBO and unmapped-memory resilience.
 */

#ifndef XI_CORE_FUNC_HPP
#define XI_CORE_FUNC_HPP

#include "Primitives.hpp"
#if !defined(__KERNEL__) && !defined(XI_NO_STD) && !defined(MECA_EMBEDDED)
#include <cstddef>
#if defined(__linux__)
#include <unistd.h>
#include <sys/syscall.h>
#include <errno.h>
#endif
#endif

namespace Xi {

template <typename T> class Func;

template <typename R, typename... Args> class XI_EXPORT Func<R(Args...)> {
private:
  static constexpr usz SBO_Size = 128; ///< Threshold for SBO.

  union Storage {
    alignas(16) u8 local[SBO_Size];
    void *heap;
  } data;

  // Direct function pointers for single-indirection execution (removes VTable completely)
  R (*invoke_ptr)(void *, Args...);
  void (*manager_ptr)(void *, void *, int); 
  bool is_heap;

  // --- Safe Pointer Verification Helper ---
  static inline bool isPointerReadable(const void* ptr) {
    if (!ptr) return false;
#if defined(__linux__) && !defined(__wasm__) && !defined(__EMSCRIPTEN__) && !defined(MECA_EMBEDDED) && !defined(XI_NO_STD)
    // Try to write 1 byte from ptr to an invalid file descriptor (-1).
    // If the pointer is invalid/unmapped, the kernel returns EFAULT.
    // If the pointer is valid, the kernel returns EBADF.
    long ret = ::syscall(SYS_write, -1, ptr, 1);
    if (ret < 0 && errno == EFAULT) {
        return false; // Pointer is unmapped/dangling (e.g. library unloaded)
    }
#endif
    return true; // Pointer is mapped and safe to dereference
  }

  // --- Unified Lifecycle Manager Operations ---
  // op 0: destroy src
  // op 1: clone src to dst
  // op 2: move src to dst and destroy src
  template <typename Callable>
  static void manager_fn(void *src, void *dst, int op) {
    using Decayed = typename Xi::Decay<Callable>::Type;
    if (op == 0) {
      static_cast<Decayed *>(src)->~Decayed();
    } else if (op == 1) {
      new (dst) Decayed(*static_cast<const Decayed *>(src));
    } else if (op == 2) {
      new (dst) Decayed(Xi::Move(*static_cast<Decayed *>(src)));
      static_cast<Decayed *>(src)->~Decayed();
    }
  }

  template <typename Callable> static R invoke_fn(void *ptr, Args... args) {
    using Decayed = typename Xi::Decay<Callable>::Type;
    return (*static_cast<Decayed *>(ptr))(args...);
  }

public:
  /** @brief Constructs an empty (invalid) function. */
  Func() : invoke_ptr(nullptr), manager_ptr(nullptr), is_heap(false) {
    data.heap = nullptr;
  }

  /** @brief Constructs from a raw function pointer. */
  Func(R (*f)(Args...)) {
    using DecayedF = R (*)(Args...);
    invoke_ptr = invoke_fn<DecayedF>;
    manager_ptr = manager_fn<DecayedF>;
    new (data.local) DecayedF(f);
    is_heap = false;
  }

  /** @brief Constructs from any callable object (lambdas, functors). */
  template <typename Callable> Func(Callable f) {
    using DecayedF = typename Xi::Decay<Callable>::Type;
    invoke_ptr = invoke_fn<DecayedF>;
    manager_ptr = manager_fn<DecayedF>;

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
  Func(Func &&o) noexcept : invoke_ptr(o.invoke_ptr), manager_ptr(o.manager_ptr), is_heap(o.is_heap) {
    if (manager_ptr) {
      if (is_heap) {
        data.heap = o.data.heap;
      } else {
        if (isPointerReadable((const void*)manager_ptr)) {
          manager_ptr((void *)o.data.local, (void *)data.local, 2); // op 2 = move + destroy
        } else {
          invoke_ptr = nullptr;
          manager_ptr = nullptr;
          is_heap = false;
        }
      }
      o.invoke_ptr = nullptr;
      o.manager_ptr = nullptr;
      o.is_heap = false;
      o.data.heap = nullptr;
    }
  }

  /** @brief Copy constructor. */
  Func(const Func &o) : invoke_ptr(o.invoke_ptr), manager_ptr(o.manager_ptr), is_heap(o.is_heap) {
    if (manager_ptr) {
      if (isPointerReadable((const void*)manager_ptr)) {
        const void *src = is_heap ? o.data.heap : (const void *)o.data.local;
        manager_ptr((void *)src, (void *)&data, 1); // op 1 = clone
      } else {
        invoke_ptr = nullptr;
        manager_ptr = nullptr;
        is_heap = false;
        data.heap = nullptr;
      }
    }
  }

  /** @brief Copy Assignment operator. */
  Func &operator=(const Func &o) {
    if (this != &o) {
      _clear();
      invoke_ptr = o.invoke_ptr;
      manager_ptr = o.manager_ptr;
      is_heap = o.is_heap;
      if (manager_ptr) {
        if (isPointerReadable((const void*)manager_ptr)) {
          const void *src = is_heap ? o.data.heap : (const void *)o.data.local;
          manager_ptr((void *)src, (void *)&data, 1); // op 1 = clone
        } else {
          invoke_ptr = nullptr;
          manager_ptr = nullptr;
          is_heap = false;
          data.heap = nullptr;
        }
      }
    }
    return *this;
  }

  /** @brief Move Assignment operator. */
  Func &operator=(Func &&o) noexcept {
    if (this != &o) {
      _clear();
      invoke_ptr = o.invoke_ptr;
      manager_ptr = o.manager_ptr;
      is_heap = o.is_heap;
      if (manager_ptr) {
        if (is_heap) {
          data.heap = o.data.heap;
        } else {
          if (isPointerReadable((const void*)manager_ptr)) {
            manager_ptr((void *)o.data.local, (void *)data.local, 2); // op 2 = move + destroy
          } else {
            invoke_ptr = nullptr;
            manager_ptr = nullptr;
            is_heap = false;
          }
        }
        o.invoke_ptr = nullptr;
        o.manager_ptr = nullptr;
        o.is_heap = false;
        o.data.heap = nullptr;
      }
    }
    return *this;
  }

  /** @brief Destructor. */
  ~Func() { _clear(); }

  /** @brief Clears the current function and releases resources. */
  void _clear() {
    if (manager_ptr) {
      if (isPointerReadable((const void*)manager_ptr)) {
        void *target = is_heap ? data.heap : (void *)data.local;
        manager_ptr(target, nullptr, 0); // op 0 = destroy
      }
      if (is_heap) {
        ::operator delete(data.heap);
      }
    }
    invoke_ptr = nullptr;
    manager_ptr = nullptr;
    is_heap = false;
  }

  /** @brief Invokes the wrapped callable. */
  R operator()(Args... args) const {
    if (!invoke_ptr || !isPointerReadable((const void*)invoke_ptr))
      return R();
    void *target = is_heap ? data.heap : (void *)data.local;
    return invoke_ptr(target, args...);
  }

  /** @brief Returns true if the function wrapper is valid. */
  bool isValid() const { return invoke_ptr != nullptr && isPointerReadable((const void*)invoke_ptr); }

  /** @brief Explicit boolean conversion for validity check. */
  operator bool() const { return isValid(); }
};

} // namespace Xi

#endif // XI_CORE_FUNC_HPP