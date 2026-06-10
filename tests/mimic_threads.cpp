#include <cstdio>
#include <cstdint>
#include "Execution/Task.hpp"

using namespace Execution;
using namespace Xi;

static int activeThreads = 0;

void threadWork(int threadId) {
    std::printf("[Thread %d] Woke up and running work!\n", threadId);
    activeThreads--;
}

void threadEntry(void* arg) {
    int id = (int)(intptr_t)arg;
    std::printf("[Thread %d] Started, now waiting for wake-up message...\n", id);

    // Call wait to put itself to sleep, flipping the switch.
    // When a message is received, it will wake up and execute threadWork(id).
    Task::current().wait(threadWork, id);
}

int main() {
    std::printf("=== Mimicking 6 Threads Waiting Use-Case ===\n");

    // Call yield() once to auto-enable core 0 before spawning threads
    Task::yield<void>();

    Task root = Task::root();
    Task threads[6];

    activeThreads = 6;

    // Spawn 6 threads
    for (int i = 0; i < 6; ++i) {
        threads[i] = root.spawn();
        threads[i].alloc(0, 65536); // Manually alloc.
        threads[i].jump(threadEntry, (void*)(intptr_t)i);
    }

    // Let them run so they can print their start message and go to wait sleep
    std::printf("[Main] Scheduling threads to go to sleep...\n");
    for (int step = 0; step < 10; ++step) {
        Task::yield<void>();
    }

    // Now, send wake-up messages to each thread
    for (int i = 0; i < 6; ++i) {
        std::printf("[Main] Sending wake-up message to Thread %d...\n", i);
        root.send(threads[i], "WAKE");
        
        // Yield to allow the awoken thread to execute
        Task::yield<void>();
    }

    std::printf("[Main] Finalizing execution...\n");
    while (activeThreads > 0) {
        Task::yield<void>();
    }

    std::printf("=== Use-Case Completed Successfully ===\n");
    return 0;
}
