/**
 * @file realistic_dev_test.cpp
 * @brief Realistic developer use-case test.
 *
 * Demonstrates how a lazy developer would use the Task subsystem:
 *   1. Spawning background tasks with simple function overloads.
 *   2. Message sending/inbox IPC using static and dynamic APIs.
 *   3. Enforcing W^X, instruction bans, and fork bomb protection automatically.
 *   4. Wait/yield cooperative scheduling.
 */

#include <cstdio>
#include <cassert>
#include <cstdint>
#include "Execution/Task.hpp"

using namespace Execution;
using namespace Xi;
using namespace Collection;

// Some lazy developer functions
void backgroundWorker(void* arg) {
    int id = (int)(intptr_t)arg;
    std::printf("[Worker %d] Starting lazy job...\n", id);

    // Yield to let others run
    Task::yield();

    std::printf("[Worker %d] Sending log update...\n", id);
    Task::current().log().push("Finished worker job");

    // Send a message to root (parentId = 0)
    Task parent = Task::findTask(0);
    Task::current().send(parent, "Done!");
}

void fibonacciTask(int n) {
    std::printf("[Fibonacci] Computing fib(%d)...\n", n);
    int a = 0, b = 1;
    for (int i = 0; i < n; ++i) {
        int temp = a + b;
        a = b;
        b = temp;
    }
    std::printf("[Fibonacci] Result: %d\n", a);
    Task::current().log().push(String("Result is ") + String(a));
}

int main() {
    std::printf("=== Realistic Developer Use-Case Test ===\n");

    // Initialize core 0 for scheduling
    Task::setup(0, false);

    // Get handle to current task (root)
    Task root = Task::current();
    std::printf("[Main] Root task ID is %d\n", (int)root.id());

    // 1. Spawning raw function pointers (very lazy)
    std::printf("[Main] Spawning background worker tasks...\n");
    Task worker1 = root.spawn(backgroundWorker, (void*)1);
    Task worker2 = root.spawn(backgroundWorker, (void*)2);

    // 2. Spawning C++ lambdas / functions with arbitrary arguments (even lazier)
    std::printf("[Main] Spawning fibonacci calculation task...\n");
    Task fib = root.spawn(fibonacciTask, 10);

    // Let the tasks start/run
    std::printf("[Main] Yielding control to background tasks...\n");
    Task::yield(0);
    Task::yield(0);

    // Verify worker tasks ran and logged output
    assert(worker1.log().size() > 0);
    std::printf("[Main] Worker 1 Log: %s\n", worker1.log()[0].c_str());
    assert(worker2.log().size() > 0);
    std::printf("[Main] Worker 2 Log: %s\n", worker2.log()[0].c_str());

    // Verify fibonacci task ran
    assert(fib.log().size() > 0);
    std::printf("[Main] Fibonacci Log: %s\n", fib.log()[0].c_str());

    // 3. IPC (Inbox/Outbox)
    std::printf("[Main] Checking inbox messages...\n");
    assert(root.inbox().size() >= 2);
    std::printf("[Main] Received: %s from Task %d\n", root.inbox()[0].payload.c_str(), (int)root.inbox()[0].senderId);
    std::printf("[Main] Received: %s from Task %d\n", root.inbox()[1].payload.c_str(), (int)root.inbox()[1].senderId);

    // 4. Fork bomb protection
    std::printf("[Main] Testing fork bomb protection (minChildQuota)...\n");
    root.setQuota(10000); // Set parent quota limit
    root.setMinChildQuota(4000); // Each child must get at least 4000us
    
    Task child1 = root.spawn(); // child1 gets 4000us, parent remaining capacity: 6000us
    Task child2 = root.spawn(); // child2 gets 4000us, parent remaining capacity: 2000us
    Task child3 = root.spawn(); // child3 spawn FAILS (requires 4000us, only 2000us left)

    std::printf("[Main] Child 1 spawn: %s\n", child1.valid() ? "SUCCESS" : "FAILED");
    std::printf("[Main] Child 2 spawn: %s\n", child2.valid() ? "SUCCESS" : "FAILED");
    std::printf("[Main] Child 3 spawn: %s\n", child3.valid() ? "SUCCESS" : "FAILED");

    assert(child1.valid() && child2.valid() && !child3.valid());

    // Clean up
    worker1.destroy();
    worker2.destroy();
    fib.destroy();
    child1.destroy();
    child2.destroy();

    std::printf("=== Realistic Developer Use-Case Test Completed Successfully ===\n");
    return 0;
}
