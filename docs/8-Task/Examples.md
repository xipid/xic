# Task Subsystem Code Examples

These examples demonstrate common usage patterns of the `Task::Task` subsystem, including basic execution, memory sandboxing, and inter-process communication.

## 1. Basic Task Spawning

This example demonstrates how to spawn a child task, set up its entry arguments, jump to a target worker function, and drive the scheduler on a CPU core.

```cpp
#include <cstdio>
#include "Task/Task.hpp"

using namespace Task;

void worker(void* arg) {
    int count = *static_cast<int*>(arg);
    std::printf("Hello from Task! Argument value: %d\n", count);
}

int main() {
    // Obtain the root task handle
    Task root = Task::root();

    // Spawn a child task in the Created state
    Task child = root.spawn();

    int argument_value = 42;
    // Set program counter and pass arguments
    child.jump(worker, &argument_value);

    // Enable scheduling on core 0
    Task::enable(0);

    // Yield control to the scheduler to run ready tasks
    Task::yield(0);

    // Reclaim task resources
    child.destroy();
    return 0;
}
```

---

## 2. Sandbox Creation and Full Memory Isolation

This example demonstrates how to configure a task in Full Isolation mode. The task's default memory maps are cleared, and a single, isolated memory region is allocated at virtual address 0.

```cpp
#include "Task/Task.hpp"

using namespace Task;

void untrusted_code(void* arg) {
    // Attempting to access parent memory outside the sandbox boundary
    // will trigger a Software Fault Isolation trap.
    volatile int* illegal_ptr = (int*)0x7FFF0000;
    *illegal_ptr = 99; // Will trigger a ud2 trap
}

int main() {
    Task root = Task::root();
    Task sandbox = root.spawn();

    // Transition the task to Full Isolation mode (isIsolated = true)
    // This clears all inherited parent memory mappings.
    sandbox.unmap();

    // Allocate 4KB of writable, non-executable memory at virtual address 0
    sandbox.alloc(0, 4096);

    // Initialize the task to run the untrusted payload
    sandbox.jump(untrusted_code, nullptr);
    sandbox.resume();

    // Drive execution on core 0
    Task::enable(0);
    Task::yield(0);

    // Cleanup
    sandbox.destroy();
    return 0;
}
```

---

## 3. IPC Message Passing (Wait and Wake)

This example demonstrates cooperative task yielding and IPC communication. The child task suspends itself to wait for an incoming message, and the parent wakes it by sending a payload.

```cpp
#include <cstdio>
#include "Task/Task.hpp"

using namespace Task;

void listener(void* arg) {
    Task self = Task::current();
    std::printf("Listener task waiting for message...\n");
    
    // Suspend the task and yield CPU control until a message is received
    self.wait();

    // Process incoming messages
    if (self.inbox().size() > 0) {
        std::printf("Message received: %s\n", self.inbox()[0].payload.c_str());
    }
}

int main() {
    // Enable the scheduler core
    Task::enable(0);

    Task root = Task::root();
    
    // Spawn and automatically start the listener task
    Task child = root.spawn(listener, nullptr);

    // Yield to let the listener run and suspend itself
    Task::yield(0);

    // Send a message to the child. This automatically transitions
    // the child back to Ready and enqueues it.
    root.send(child, "Activate!");

    // Yield to let the listener process the message
    Task::yield(0);

    // Cleanup
    child.destroy();
    return 0;
}
```
