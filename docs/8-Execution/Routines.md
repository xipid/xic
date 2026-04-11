# Cooperative Routines

The **Execution::Routine** module provides a high-performance coroutine and task scheduling system. It allows you to write asynchronous logic that looks and feels like synchronous code, without the heavy overhead of OS-level threads.

---

## The Routine System

A `Routine` is a type-erased task that can be scheduled to run on one or more CPU cores. Unlike standard threads, xic routines are **cooperative**, meaning they only yield control back to the scheduler when they hit a suspension point (like an `await()` or `pause()` call).

### Key Features
-   **Zero-Allocation Lambdas**: Powered by `Xi::Func` for minimal heap fragmentation.
-   **Core Affinity**: You can pin routines to specific hardware cores (essential for ESP32-S3).
-   **Yield Streams**: Every routine can "yield" data back to its parent through a built-in `VirtualStream`.

---

## Basic Execution

You can start a new routine using the `run` method. It returns a `Routine` handle that you can use to monitor or control the task.

```cpp
using namespace Execution;

// Start a simple background task
Routine task = Routine::run([]() {
    for (int i = 0; i < 5; i++) {
        info("Heartbeat: " + String(i));
        Routine::current().pause(1000); // Sleep for 1s
    }
});

// Wait for it to finish
task.await();
```

---

## Communication (Yielding)

Routines can communicate with the outside world by pushing data to their internal stream. This is a "Generator" pattern implemented at the library level.

```cpp
Routine counter = Routine::run([]() {
    for (int i = 0; i < 10; i++) {
        push(String(i)); // Pushes to the yield stream
    }
});

// Read from the counter as it produces data
while (counter.size() > 0) {
    String val = counter.shift();
}
```

---

## The Scheduler

To drive your routines, you must start the xic scheduler. On native systems, this usually happens in your `main()` function.

```cpp
int main() {
    // Start background routines...
    Routine::run(myBackgroundTask);

    // Block the main thread and run the scheduler on all available cores
    Routine::startSchedulers();
    return 0;
}
```

---

## Best Practices

1.  **Don't Block**: Never use `std::this_thread::sleep_for` or blocking OS calls inside a routine. Use `Routine::pause()` to yield control properly.
2.  **State Management**: Avoid using raw pointers to stack variables inside a lambda. Capture members by value or use reference-counted containers (like `String` or `Array`).
3.  **Core Balancing**: If you have a high-priority task (like audio processing), pin it to a dedicated core using the affinity mask in `RoutineState`.
