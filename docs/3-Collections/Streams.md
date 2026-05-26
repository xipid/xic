# Polymorphic Streams

The **Collection::Stream** module provides a generic abstraction for data sources and sinks. It is designed to bridge the gap between contiguous arrays and asynchronous event-driven data flows.

---

## VirtualStream

`VirtualStream<T>` is an interface for any type that can be pushed to, shifted from, or observed.

### Core Interface
-   **`push(val)`**: Add to end.
-   **`unshift(val)`**: Add to start.
-   **`shift()`**: Remove from start.
-   **`pop()`**: Remove from end.

### Event Listeners
You can attach a callback to the `onPush` member to be notified whenever new data enters the stream. This is powered by `Xi::Func`, ensuring minimal overhead and no heap fragmentation for most callbacks.

```cpp
VirtualStream<u8> *myStream = ...;

myStream->onPush = [](const u8 &byte) {
    printf("Received: %02X\n", byte);
};
```

---

## Generator Support

xic streams support C++ iterator patterns, allowing them to be used in range-based for loops. Note that iterating over a stream typically "consumes" it (shifting items as it goes).

```cpp
for (auto item : *myStream) {
    // item was shifted from myStream
}
```

---

## C++20 Stream Concept

For modern codebases, xic defines a `Stream` concept. This allows you to write template functions that work with `Array`, `InlineArray`, or `VirtualStream` interchangeably.

```cpp
template <Stream<u8> S>
void processData(S &stream) {
    if (stream.size() > 10) {
        u8 header = stream.shift();
    }
}
```

---

## Best Practices

1.  **Memory Ownership**: `VirtualStream` is a base class. Ensure you are aware of how the underlying implementation (e.g., a network socket stream or a file stream) manages its buffers.
2.  **Observer Overhead**: While `onPush` is efficient, avoid performing heavy computation inside the callback, as it may block the producer of the data.
3.  **Consumption**: Remember that unlike an `Array`, iterating over a `VirtualStream` usually empties the container. If you need to "peek" or keep the data, use an `Array` instead.
