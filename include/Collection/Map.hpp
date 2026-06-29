/**
 * @file Map.hpp
 * @brief High-performance Robin Hood hash map implementation for the Xi
 * framework.
 */

#ifndef XI_CORE_MAP_HPP
#define XI_CORE_MAP_HPP

#include "InlineArray.hpp"

namespace Collection {
  
class String;

/**
 * @struct MapEntry
 * @brief Internal container for key-value pairs in the Map.
 *
 * Uses the LSB of the fnvHash for occupancy tracking.
 */
template <typename K, typename V> struct MapEntry {
  K key;       ///< The entry key.
  V value;     ///< The entry value.
  u32 fnvHash; ///< Cached hash of the key (Bit 0 = occupied flag).

  MapEntry() : key(), value(), fnvHash(0) {}

  MapEntry(MapEntry &&o) noexcept
      : key(Xi::Move(o.key)), value(Xi::Move(o.value)), fnvHash(o.fnvHash) {
    o.fnvHash = 0;
  }

  MapEntry &operator=(MapEntry &&o) noexcept {
    if (this != &o) {
      key = Xi::Move(o.key);
      value = Xi::Move(o.value);
      fnvHash = o.fnvHash;
      o.fnvHash = 0;
    }
    return *this;
  }

  MapEntry(const MapEntry &) = delete;
  MapEntry &operator=(const MapEntry &) = delete;

  inline bool isEmpty() const { return (fnvHash & 1) == 0; }
  inline void markEmpty() { fnvHash = 0; }
  inline void setHash(u32 h) { fnvHash = (h | 1); }
};

/**
 * @class Map
 * @brief An associative container that stores key-value pairs.
 *
 * Implements a high-performance hash table using Robin Hood hashing with
 * linear probing to minimize variance in probe sequence lengths.
 */
template <typename K, typename V> class XI_EXPORT Map {
private:
  InlineArray<MapEntry<K, V>> buckets; ///< Contiguous bucket storage.
  usz count = 0;     ///< Number of elements currently in the map.
  usz capacity = 0;  ///< Total number of buckets.
  usz mask = 0;      ///< Bitmask for fast modulo (capacity - 1).
  usz threshold = 0; ///< Resize threshold (load factor).

  static constexpr usz MIN_CAPACITY = 16; ///< Minimum capacity for new maps.

  static inline u32 clean_hash(usz h) {
    u32 h32 = (u32)h;
    if (sizeof(usz) == 8)
      h32 ^= (u32)((u64)h >> 32);
    return (h32 | 1);
  }

  void allocate_buckets(usz newCap) {
    buckets.allocate(newCap);
    capacity = newCap;
    mask = newCap - 1;
    threshold = (newCap * 85) / 100;
    count = 0;
  }

  void free_buckets() { buckets.destroy(); }

  bool insert_internal(MapEntry<K, V> *slots, usz cap, usz capMask, K &&key,
                       V &&val, bool overwrite) {
    usz hRaw = FNVHasher<K>::fnvHash(key);
    u32 h = clean_hash(hRaw);
    usz idx = (usz)h & capMask;
    usz psl = 0;

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
        h = toInsert.fnvHash;
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
  Map() : count(0), capacity(0), mask(0), threshold(0) {}

  Map(const Map &other) : count(0), capacity(0) {
    allocate_buckets(other.capacity);
    for (usz i = 0; i < other.capacity; ++i) {
      if (!other.buckets[i].isEmpty())
        set(other.buckets[i].key, other.buckets[i].value);
    }
  }

  Map(Map &&other) noexcept {
    buckets = Xi::Move(other.buckets);
    count = other.count;
    capacity = other.capacity;
    mask = other.mask;
    threshold = other.threshold;
    other.count = 0;
    other.capacity = 0;
  }

  Map &operator=(Map &&other) noexcept {
    if (this != &other) {
      buckets = Xi::Move(other.buckets);
      count = other.count;
      capacity = other.capacity;
      mask = other.mask;
      threshold = other.threshold;
      other.count = 0;
      other.capacity = 0;
    }
    return *this;
  }

  Map &operator=(const Map &other) {
    if (this != &other) {
      free_buckets();
      allocate_buckets(other.capacity);
      for (usz i = 0; i < other.capacity; ++i) {
        if (!other.buckets[i].isEmpty())
          set(other.buckets[i].key, other.buckets[i].value);
      }
    }
    return *this;
  }

  ~Map() { free_buckets(); }

  usz size() const { return count; }
  usz length() const { return count; }
  bool isEmpty() const { return count == 0; }

  /**
   * @brief Associates a key with a value (updates if key exists).
   */
  void set(const K &key, const V &val) {
    K k = key;
    V v = val;
    put(Xi::Move(k), Xi::Move(v));
  }

  /**
   * @brief Associates a key with a value using move semantics.
   */
  void put(K key, V val) {
    if (capacity == 0)
      allocate_buckets(MIN_CAPACITY);
    if (count >= threshold)
      resize(capacity * 2);
    if (insert_internal(buckets.data(), capacity, mask, Xi::Move(key),
                        Xi::Move(val), true))
      count++;
  }

  /**
   * @brief Retrieves a pointer to the value associated with a key.
   * @return Pointer to value, or nullptr if not found.
   */
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

  /**
   * @brief Checks if a key exists in the map.
   */
  bool has(const K &key) const { return get(key) != nullptr; }

  /**
   * @brief Subscript operator for access or insertion.
   */
  V &operator[](const K &key) {
    V *existing = get(key);
    if (existing)
      return *existing;
    put(key, V());
    return *get(key);
  }

  /**
   * @brief Removes a key and its associated value.
   * @return true if key was removed.
   */
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

  /**
   * @brief Clears all entries from the map.
   */
  void clear() {
    if (count == 0)
      return;
    for (usz i = 0; i < capacity; ++i)
      if (!buckets[i].isEmpty())
        buckets[i] = MapEntry<K, V>();
    count = 0;
  }

  // --- Iterators ---

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

  Iterator begin() {
    return Iterator(buckets.data(), buckets.data() + capacity);
  }
  Iterator end() {
    return Iterator(buckets.data() + capacity, buckets.data() + capacity);
  }
  ConstIterator begin() const {
    return ConstIterator(buckets.data(), buckets.data() + capacity);
  }
  ConstIterator end() const {
    return ConstIterator(buckets.data() + capacity, buckets.data() + capacity);
  }

  // --- Serialization ---

  /**
   * @brief Serializes the map to a string (deterministic order).
   */
  template <typename S = String> S serialize() const {
    S s;
    s.pushVarLong((long long)count);
    InlineArray<K> keys_arr;
    for (auto &kv : *this)
      keys_arr.push(kv.key);
    // Simple bubble sort for deterministic serialization
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

} // namespace Collection

#endif // XI_CORE_MAP_HPP