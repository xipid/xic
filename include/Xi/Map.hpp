#ifndef XI_MAP_HPP
#define XI_MAP_HPP 1

#include "String.hpp"

// Serialization integrated into Map.

namespace Xi {

// -----------------------------------------------------------------------------
// MapEntry
// Container for Key-Value pairs.
// -----------------------------------------------------------------------------
template <typename K, typename V> struct MapEntry {
  K key;
  V value;
  u32 fnvHash; // LSB (bit 0) is used as the "Occupied" flag.

  MapEntry() : key(), value(), fnvHash(0) {}

  // 1. Move Constructor
  // Essential for resizing without deep copying keys/values.
  MapEntry(MapEntry &&o) noexcept
      : key(Xi::Move(o.key)), value(Xi::Move(o.value)), fnvHash(o.fnvHash) {
    o.fnvHash = 0;
  }

  // 2. Move Assignment
  // Essential for "Robin Hood" swapping logic.
  MapEntry &operator=(MapEntry &&o) noexcept {
    if (this != &o) {
      key = Xi::Move(o.key);
      value = Xi::Move(o.value);
      fnvHash = o.fnvHash;
      o.fnvHash = 0;
    }
    return *this;
  }

  // Disable Copying to prevent accidental heavy operations
  MapEntry(const MapEntry &) = delete;
  MapEntry &operator=(const MapEntry &) = delete;

  inline bool isEmpty() const { return (fnvHash & 1) == 0; }
  inline void markEmpty() { fnvHash = 0; }

  // Sets hash and ensures LSB is 1
  inline void setHash(u32 h) { fnvHash = (h | 1); }
};

// -----------------------------------------------------------------------------
// Map
// High-Performance Robin Hood Hash Table
// -----------------------------------------------------------------------------
template <typename K, typename V> class XI_EXPORT Map {
private:
  InlineArray<MapEntry<K, V>>
      buckets; // Using InlineArray instead of ArrayFragment

  usz count;
  usz capacity;
  usz mask;
  usz threshold;

  static constexpr usz MIN_CAPACITY = 16; // Must be Power of 2

  // Helper: Folds 64-bit hash to 32-bit and ensures LSB is 1
  static inline u32 clean_hash(usz h) {
    u32 h32 = (u32)h;
    if (sizeof(usz) == 8) {
      h32 ^= (u32)((u64)h >> 32);
    }
    return (h32 | 1);
  }

  void allocate_buckets(usz newCap) {
    buckets.allocate(newCap);
    // InlineArray::alloc constructs default elements efficiently?
    // MapEntry default ctor is cheap.
    // However, alloc logic in InlineArray handles construction.
    // We don't need manual placement new loop unless we used raw alloc.

    // Note: InlineArray manages lifetime.

    capacity = newCap;
    mask = newCap - 1;
    threshold = (newCap * 85) / 100; // 85% Load Factor
    count = 0;
  }

  void free_buckets() { buckets.destroy(); }

  // Core Insertion Logic
  // Returns: true if inserted new, false if updated existing.
  bool insert_internal(MapEntry<K, V> *slots, usz cap, usz capMask, K &&key,
                       V &&val, bool overwrite) {
    usz hRaw = FNVHasher<K>::fnvHash(key);
    u32 h = clean_hash(hRaw);
    usz idx = (usz)h & capMask;
    usz psl = 0; // Probe Sequence Length

    MapEntry<K, V> toInsert;
    toInsert.key = Xi::Move(key);
    toInsert.value = Xi::Move(val);
    toInsert.setHash(h);

    for (usz i = 0; i < cap; ++i) {
      MapEntry<K, V> &slot = slots[idx];

      if (slot.isEmpty()) {
        slot = Xi::Move(toInsert);
        return true;
      }

      if (slot.fnvHash == h && Equal<K>::eq(slot.key, toInsert.key)) {
        if (overwrite)
          slot.value = Xi::Move(toInsert.value);
        return false;
      }

      usz slotHome = (usz)(slot.fnvHash) & capMask;
      usz slotPSL = (idx - slotHome) & capMask;

      if (psl > slotPSL) {
        Xi::Swap(toInsert, slot);
        psl = slotPSL;
      }

      idx = (idx + 1) & capMask;
      psl++;
    }
    return true;
  }

  void resize(usz newCap) {
    InlineArray<MapEntry<K, V>> oldBuckets = Xi::Move(buckets);
    usz oldCap = capacity;

    allocate_buckets(newCap);

    if (oldBuckets.data()) {
      for (usz i = 0; i < oldCap; ++i) {
        MapEntry<K, V> &e = oldBuckets[i];
        if (!e.isEmpty()) {
          insert_internal(buckets.data(), capacity, mask, Xi::Move(e.key),
                          Xi::Move(e.value), true);
          count++;
        }
      }
    }
  }

public:
  // -------------------------------------------------------------------------
  // Constructors
  // -------------------------------------------------------------------------
  Map() : count(0), capacity(0) { allocate_buckets(MIN_CAPACITY); }

  Map(const Map &other) : count(0), capacity(0) {
    allocate_buckets(other.capacity);
    for (usz i = 0; i < other.capacity; ++i) {
      if (!other.buckets[i].isEmpty())
        set(other.buckets[i].key, other.buckets[i].value);
    }
  }

  Map(Map &&other) {
    buckets = Xi::Move(other.buckets);
    count = other.count;
    capacity = other.capacity;
    mask = other.mask;
    threshold = other.threshold;

    other.count = 0;
    other.capacity = 0;
  }

  Map &operator=(Map &&other) {
    if (this != &other) {
      buckets = Xi::Move(other.buckets);
      count = other.count;
      capacity = other.capacity;
      mask = other.mask;
      threshold = other.threshold;
      other.count = 0;
    }
    return *this;
  }

  ~Map() { free_buckets(); }

  // -------------------------------------------------------------------------
  // Public API
  // -------------------------------------------------------------------------

  usz size() const { return count; }
  usz length() const { return count; }

  void set(const K &key, const V &val) {
    K k = key;
    V v = val;
    put(Xi::Move(k), Xi::Move(v));
  }

  void put(K key, V val) {
    if (count >= threshold)
      resize(capacity * 2);

    bool isNew = insert_internal(buckets.data(), capacity, mask, Xi::Move(key),
                                 Xi::Move(val), true);
    if (isNew)
      count++;
  }

  V *get(const K &key) {
    if (count == 0)
      return nullptr;

    usz hRaw = FNVHasher<K>::fnvHash(key);
    u32 h = clean_hash(hRaw);
    usz idx = (usz)h & mask;
    usz dist = 0;

    for (usz i = 0; i < capacity; ++i) {
      MapEntry<K, V> &slot = buckets[idx];

      if (slot.isEmpty())
        return nullptr;

      if (slot.fnvHash == h && Equal<K>::eq(slot.key, key))
        return &slot.value;

      usz slotHome = (usz)(slot.fnvHash) & mask;
      usz slotDist = (idx - slotHome) & mask;

      if (dist > slotDist)
        return nullptr;

      idx = (idx + 1) & mask;
      dist++;
    }
    return nullptr;
  }

  const V *get(const K &key) const { return const_cast<Map *>(this)->get(key); }

  bool has(const K &key) const { return get(key) != nullptr; }

  V &operator[](const K &key) {
    V *existing = get(key);
    if (existing)
      return *existing;

    put(key, V());
    return *get(key);
  }

  bool remove(const K &key) {
    if (count == 0)
      return false;

    usz hRaw = FNVHasher<K>::fnvHash(key);
    u32 h = clean_hash(hRaw);
    usz idx = (usz)h & mask;
    usz dist = 0;

    for (usz i = 0; i < capacity; ++i) {
      MapEntry<K, V> &slot = buckets[idx];

      if (slot.isEmpty())
        return false;

      usz slotHome = (usz)(slot.fnvHash) & mask;
      usz slotDist = (idx - slotHome) & mask;

      if (dist > slotDist)
        return false;

      if (slot.fnvHash == h && Equal<K>::eq(slot.key, key)) {
        count--;
        usz nextIdx = (idx + 1) & mask;

        for (usz j = 0; j < capacity; ++j) {
          MapEntry<K, V> &nextSlot = buckets[nextIdx];

          if (nextSlot.isEmpty()) {
            buckets[idx] = MapEntry<K, V>();
            return true;
          }

          usz nextHome = (usz)(nextSlot.fnvHash) & mask;
          usz distFromHome = (nextIdx - nextHome) & mask;

          if (distFromHome == 0) {
            buckets[idx] = MapEntry<K, V>();
            return true;
          }

          buckets[idx] = Xi::Move(nextSlot);

          idx = nextIdx;
          nextIdx = (nextIdx + 1) & mask;
        }
        return true;
      }

      idx = (idx + 1) & mask;
      dist++;
    }
    return false;
  }

  void clear() {
    if (count == 0)
      return;
    for (usz i = 0; i < capacity; ++i) {
      if (!buckets[i].isEmpty())
        buckets[i] = MapEntry<K, V>();
    }
    count = 0;
  }

  // -------------------------------------------------------------------------
  // Iterators
  // -------------------------------------------------------------------------
  struct Iterator {
    MapEntry<K, V> *ptr;
    MapEntry<K, V> *endPtr;

    Iterator(MapEntry<K, V> *p, MapEntry<K, V> *e) : ptr(p), endPtr(e) {
      if (ptr && ptr < endPtr && ptr->isEmpty())
        ++(*this);
    }

    bool operator!=(const Iterator &o) const { return ptr != o.ptr; }

    Iterator &operator++() {
      do {
        ptr++;
      } while (ptr < endPtr && ptr->isEmpty());
      return *this;
    }

    MapEntry<K, V> &operator*() { return *ptr; }
    MapEntry<K, V> *operator->() { return ptr; }
  };

  Iterator begin() {
    return Iterator(buckets.data(), buckets.data() + capacity);
  }
  Iterator end() {
    return Iterator(buckets.data() + capacity, buckets.data() + capacity);
  }

  struct ConstIterator {
    const MapEntry<K, V> *ptr;
    const MapEntry<K, V> *endPtr;

    ConstIterator(const MapEntry<K, V> *p, const MapEntry<K, V> *e)
        : ptr(p), endPtr(e) {
      if (ptr && ptr < endPtr && ptr->isEmpty())
        ++(*this);
    }

    bool operator!=(const ConstIterator &o) const { return ptr != o.ptr; }

    ConstIterator &operator++() {
      do {
        ptr++;
      } while (ptr < endPtr && ptr->isEmpty());
      return *this;
    }

    const MapEntry<K, V> &operator*() const { return *ptr; }
    const MapEntry<K, V> *operator->() const { return ptr; }
  };

  ConstIterator begin() const {
    return ConstIterator(buckets.data(), buckets.data() + capacity);
  }
  ConstIterator end() const {
    return ConstIterator(buckets.data() + capacity, buckets.data() + capacity);
  }

  // Implicit copy assignment needs to be handled if Map is copy-assignable?
  // Map(const Map&) is defined. operator=(const Map&) is implicit?
  // operator=(Map&&) is defined.
  // Best to define copy assignment if copy ctor is present.
  Map &operator=(const Map &other) {
    if (this != &other) {
      allocate_buckets(other.capacity);
      for (usz i = 0; i < other.capacity; ++i) {
        if (!other.buckets[i].isEmpty())
          set(other.buckets[i].key, other.buckets[i].value);
      }
    }
    return *this;
  }

  // --- Serialization ---
  template <typename S = String> S serialize() const {
    S s;
    s.pushVarLong((long long)count);

    // Gather keys and sort them to guarantee deterministic serialization order
    InlineArray<K> keys_arr;
    for (auto &kv : *this) {
      keys_arr.push(kv.key);
    }
    for (usz i = 0; i < keys_arr.size(); ++i) {
      for (usz j = i + 1; j < keys_arr.size(); ++j) {
        if (keys_arr[j] < keys_arr[i]) {
          K tmp = keys_arr[i];
          keys_arr[i] = keys_arr[j];
          keys_arr[j] = tmp;
        }
      }
    }

    for (usz i = 0; i < keys_arr.size(); ++i) {
      const K &k = keys_arr[i];
      s += Xi::serialize<K>(k);
      s += Xi::serialize<V>(*get(k));
    }
    return s;
  }

  template <typename S = String> static Map<K, V> deserialize(const S &s) {
    usz at = 0;
    return deserialize(s, at);
  }

  template <typename S = String>
  static Map<K, V> deserialize(const S &s, usz &at) {
    Map<K, V> m;
    auto countRes = s.peekVarLong(at);
    if (countRes.error)
      return m;
    at += countRes.bytes;

    for (usz i = 0; i < (usz)countRes.value; ++i) {
      K key = Xi::deserialize<K, S>(s, at);
      V val = Xi::deserialize<V, S>(s, at);
      m.put(Xi::Move(key), Xi::Move(val));
    }
    return m;
  }
};

// Removed Encoding namespace implementations.
} // namespace Xi

#endif // XI_MAP_HPP