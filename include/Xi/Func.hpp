#ifndef XI_FUNC_HPP
#define XI_FUNC_HPP

#include "Primitives.hpp"
#include <cstddef>

namespace Xi {

template <typename T> class Func;

template <typename R, typename... Args> class XI_EXPORT Func<R(Args...)> {
private:
  static constexpr usz SBO_Size = 128;

  struct VTable {
    R (*invoke)(void *, Args...);
    void (*destroy)(void *);
    void (*clone)(const void *, void *);
  };

  // Internal union to handle either local storage or a heap pointer
  union Storage {
    alignas(max_align_t) u8 local[SBO_Size];
    void *heap;
  } data;

  const VTable *vptr;
  bool is_heap;

  // --- Implementation Helpers ---
  template <typename Callable> static R invoke_fn(void *ptr, Args... args) {
    // If heap, ptr is the address of the pointer itself, so we dereference
    Callable *func = static_cast<Callable *>(ptr);
    return (*func)(args...);
  }

  static inline u32 clean_hash(usz h) {
    u32 h32 = (u32)h;
    if (sizeof(usz) == 8) {
      h32 ^= (u32)((u64)h >> 32);
    }
    return (h32 | 1);
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
      // Heap clone
      Callable *copy = (Callable *)::operator new(sizeof(Callable));
      new (copy) Callable(*source);
      *(void **)dst = (void *)copy;
    }
  }

public:
  Func() : vptr(null), is_heap(false) { data.heap = null; }

  // Add this to your public section in XiFunc.hpp
  Func(R (*f)(Args...)) {
    using DecayedF = R (*)(Args...);
    static const VTable vt = {invoke_fn<DecayedF>, destroy_fn<DecayedF>,
                              clone_fn<DecayedF>};
    vptr = &vt;

    // Function pointers always fit in SBO_Size
    new (data.local) DecayedF(f);
    is_heap = false;
  }

  template <typename Callable> Func(Callable f) {
    using DecayedF = Callable;
    static const VTable vt = {invoke_fn<DecayedF>, destroy_fn<DecayedF>,
                              clone_fn<DecayedF>};
    vptr = &vt;

    if (sizeof(DecayedF) <= SBO_Size) {
      new (data.local) DecayedF(Xi::Move(f));
      is_heap = false;
    } else {
      // Allocation logic if capture is too large
      DecayedF *heap_ptr = (DecayedF *)::operator new(sizeof(DecayedF));
      new (heap_ptr) DecayedF(Xi::Move(f));
      data.heap = heap_ptr;
      is_heap = true;
    }
  }

  Func(Func &&o) noexcept : vptr(o.vptr), is_heap(o.is_heap) {
    if (is_heap) {
      data.heap = o.data.heap;
    } else {
      // Copy the local buffer bytes
      for (usz i = 0; i < SBO_Size; ++i)
        data.local[i] = o.data.local[i];
    }
    // Neutralize the source so _clear() does nothing
    o.vptr = null;
    o.is_heap = false;
    o.data.heap = null;
  }

  // Assignment Operator (Copy and Swap idiom)
  Func &operator=(Func o) {
    Xi::Swap(vptr, o.vptr);
    Xi::Swap(is_heap, o.is_heap);
    for (usz i = 0; i < SBO_Size; ++i) {
      Xi::Swap(data.local[i], o.data.local[i]);
    }
    return *this;
  }

  ~Func() { _clear(); }

  void _clear() {
    if (vptr) {
      void *target = is_heap ? data.heap : (void *)data.local;
      vptr->destroy(target);
      if (is_heap)
        ::operator delete(data.heap);
    }
    vptr = null;
  }

  // Rule of 3/5: Copy and Move
  Func(const Func &o) : vptr(o.vptr), is_heap(o.is_heap) {
    if (vptr) {
      const void *src = is_heap ? o.data.heap : (const void *)o.data.local;
      vptr->clone(src, (void *)&data);
    }
  }

  R operator()(Args... args) const {
    if (!vptr)
      return R();
    void *target = is_heap ? data.heap : (void *)data.local;
    return vptr->invoke(target, args...);
  }

  bool isValid() const { return vptr != null; }
};

} // namespace Xi

#endif