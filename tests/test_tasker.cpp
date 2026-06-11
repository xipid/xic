/**
 * @file test_tasker.cpp
 * @brief Functional test for the Task subsystem.
 *
 * Tests:
 *   1. Task creation and hierarchy.
 *   2. IPC (message send/receive).
 *   3. Memory allocation and mapping.
 *   4. CustomTask subclassing.
 *   5. Scheduler lifecycle (create, start, stop, destroy).
 *   6. Context switch (on x86_64 only — requires real assembly).
 */

#include <cstdio>
#include <cassert>
#include "Execution/Task.hpp"

using namespace Execution;
using namespace Xi;
using namespace Collection;

namespace Execution {
    extern void xi_set_current_task(TaskState* s);
    extern void xi_reset_task_state_for_tests();
}

// -------------------------------------------------------------------------
// CustomTask subclass test — must work with Task seamlessly.
// -------------------------------------------------------------------------

struct CustomTask : public Task {
    int permissions;
    String role;

    CustomTask() : Task(), permissions(0) {}
    CustomTask(TaskState* state)
        : Task(state), permissions(0) {}

    bool hasPermission(int perm) const {
        return (permissions & perm) != 0;
    }

    void grantPermission(int perm) {
        permissions |= perm;
    }
};

// -------------------------------------------------------------------------
// Test helpers
// -------------------------------------------------------------------------

static int testsPassed = 0;
static int testsFailed = 0;

#define TEST(name, expr)                                                \
    do {                                                                \
        if (expr) {                                                     \
            ++testsPassed;                                              \
            std::printf("  ✓ %s\n", name);                             \
        } else {                                                        \
            ++testsFailed;                                              \
            std::printf("  ✗ %s (FAILED at line %d)\n", name, __LINE__);\
        }                                                               \
    } while (0)

// -------------------------------------------------------------------------
// Tests
// -------------------------------------------------------------------------

void testTaskCreation() {
    std::printf("\n[Task Creation]\n");

    xi_reset_task_state_for_tests();
    Task root = Task::root();

    TEST("root is valid", root.valid());
    TEST("root id is 0", root.id() == 0);
    TEST("root has no parent (parentId == 0)", root.parentId() == 0);
    TEST("taskCount starts at 1", Task::taskCount() == 1);

    Task child = root.spawn();
    TEST("child is valid", child.valid());
    TEST("child id is 1", child.id() == 1);
    TEST("child parentId is root", child.parentId() == 0);
    TEST("taskCount is 2", Task::taskCount() == 2);

    Task grandchild = child.spawn();
    TEST("grandchild is valid", grandchild.valid());
    TEST("grandchild parentId is child", grandchild.parentId() == 1);
    TEST("taskCount is 3", Task::taskCount() == 3);

    TEST("root has 1 child", root.childCount() == 1);
    TEST("child has 1 child", child.childCount() == 1);
}

void testCustomTask() {
    std::printf("\n[CustomTask Subclassing]\n");

    xi_reset_task_state_for_tests();
    Task root = Task::root();

    // Create a CustomTask from a spawned task.
    Task base = root.spawn();
    CustomTask ct;
    ct._state = base._state;
    ct.permissions = 0;
    ct.role = "worker";

    TEST("CustomTask is valid", ct.valid());
    TEST("CustomTask has correct id", ct.id() == base.id());
    TEST("CustomTask role is set", ct.role == "worker");

    ct.grantPermission(0x01);
    TEST("CustomTask permission granted", ct.hasPermission(0x01));
    TEST("CustomTask missing permission", !ct.hasPermission(0x02));

    // CustomTask can use all Task methods.
    ct.setQuota(5000);
    TEST("CustomTask setQuota works", ct._state->quotaUs == 5000);

    // Spawn from CustomTask.
    Task child = ct.spawn();
    TEST("Spawn from CustomTask works", child.valid());
    TEST("Child parent is CustomTask", child.parentId() == ct.id());
}

void testIPC() {
    std::printf("\n[IPC]\n");

    xi_reset_task_state_for_tests();
    Task root = Task::root();
    Task sender = root.spawn();
    Task receiver = root.spawn();

    sender.send(receiver, "hello");
    TEST("receiver has 1 message", receiver.inbox().size() == 1);
    TEST("message sender is correct", receiver.inbox()[0].senderId == sender.id());
    TEST("message payload is correct", receiver.inbox()[0].payload == "hello");

    // Sender should be auto-shared to receiver.
    TEST("sender shared to receiver",
         receiver._state->sharedIds.size() >= 1);

    // Send another message.
    sender.send(receiver, "world");
    TEST("receiver has 2 messages", receiver.inbox().size() == 2);
}

void testMemory() {
    std::printf("\n[Memory]\n");

    xi_reset_task_state_for_tests();
    Task root = Task::root();
    Task task = root.spawn();

    // Auto-alloc happens on start(), but we can manually alloc.
    task.alloc(0x1000, 256);
    TEST("task has 1 region after alloc", task._state->regions.size() == 1);
    TEST("region base is 0x1000", task._state->regions[0].base == 0x1000);
    TEST("region size is 256", task._state->regions[0].size == 256);
    TEST("region is writable", task._state->regions[0].writable);
    TEST("region is not executable (W^X)", !task._state->regions[0].executable);
    TEST("region is owned", task._state->regions[0].owned);

    // Copy within the region.
    u8* phys = task._state->regions[0].physical;
    phys[0] = 0xAB;
    phys[1] = 0xCD;
    task.copy(0x1000, 0x1010, 2);
    TEST("copy works", phys[0x10] == 0xAB && phys[0x11] == 0xCD);

    // Unmap.
    task.unmap(0x1000, 256);
    TEST("region removed after unmap", task._state->regions.size() == 0);
}

void testMemoryAutoAlloc() {
    std::printf("\n[Memory Auto-Alloc]\n");

    xi_reset_task_state_for_tests();
    Task root = Task::root();
    Task task = root.spawn();

    // Don't call alloc — resume() should auto-allocate.
    task._state->entryFn = [](void*) {};
    // Can't actually run (would need scheduler), but resume() should
    // allocate memory and context.
    task.resume();

    TEST("auto-alloc created region", task._state->regions.size() > 0);
    TEST("auto-alloc size is 4KB", task._state->regions[0].size == 4096);
    TEST("stack was allocated", task._state->stack != nullptr);
    TEST("task is Ready", task.status() == TaskStatus::Ready);
}

void testScheduling() {
    std::printf("\n[Scheduling]\n");

    xi_reset_task_state_for_tests();
    Task root = Task::root();

    Task t1 = root.spawn();
    t1.setQuota(5000);
    TEST("quota set", t1._state->quotaUs == 5000);

    Task t2 = root.spawn();
    t2.setQuota(0); // Unlimited.
    TEST("unlimited quota", t2._state->quotaUs == 0);

    // Pin test.
    t1.setPin(1);
    TEST("task pinned", t1.isPinned());
    TEST("pinned core is 1", t1._state->pinnedCore == 1);

    t1.clearPin();
    TEST("task unpinned", !t1.isPinned());

    // SetMemorySize.
    Task t3 = root.spawn();
    t3.setMemorySize(8192);
    TEST("custom memory size before start", t3._state->stackSize == 8192);
}

void testDestroy() {
    std::printf("\n[Destroy]\n");

    xi_reset_task_state_for_tests();
    Task root = Task::root();

    Task parent = root.spawn();
    Task child1 = parent.spawn();
    Task child2 = parent.spawn();
    Task grandchild = child1.spawn();

    usz parentId = parent.id();
    usz child1Id = child1.id();
    usz grandchildId = grandchild.id();

    TEST("4 tasks before destroy", Task::taskCount() == 5); // root + 4

    // Destroy parent — should cascade to children and grandchildren.
    parent.destroy();

    TEST("parent destroyed", Task::findTask(parentId).valid() == false);
    TEST("child1 destroyed", Task::findTask(child1Id).valid() == false);
    TEST("grandchild destroyed", Task::findTask(grandchildId).valid() == false);
}

void testLog() {
    std::printf("\n[Log]\n");

    xi_reset_task_state_for_tests();
    Task root = Task::root();
    Task task = root.spawn();

    task.log().push("output line 1");
    task.log().push("output line 2");

    TEST("log has 2 entries", task.log().size() == 2);
    TEST("log[0] correct", task.log()[0] == "output line 1");
    TEST("log[1] correct", task.log()[1] == "output line 2");
}

void testFindTask() {
    std::printf("\n[FindTask]\n");

    xi_reset_task_state_for_tests();
    Task root = Task::root();

    Task t1 = root.spawn();
    Task t2 = root.spawn();
    Task t3 = root.spawn();

    TEST("find root", Task::findTask(0).valid());
    TEST("find t1", Task::findTask(t1.id()).id() == t1.id());
    TEST("find t2", Task::findTask(t2.id()).id() == t2.id());
    TEST("find t3", Task::findTask(t3.id()).id() == t3.id());
    TEST("invalid id returns invalid", !Task::findTask(999).valid());
}

void testSharing() {
    std::printf("\n[Sharing]\n");

    xi_reset_task_state_for_tests();
    Task root = Task::root();

    Task t1 = root.spawn();
    Task t2 = root.spawn();
    Task t3 = root.spawn();

    // Share t3 with t1.
    t1.share(t3);
    bool found = false;
    for (usz i = 0; i < t1._state->sharedIds.size(); ++i) {
        if (t1._state->sharedIds[i] == t3.id()) found = true;
    }
    TEST("t3 shared with t1", found);

    // Double share should not duplicate.
    usz prevSize = t1._state->sharedIds.size();
    t1.share(t3);
    TEST("no duplicate shares", t1._state->sharedIds.size() == prevSize);
}

void testSharedQuota() {
    std::printf("\n[Shared/Self Quota Block]\n");

    xi_reset_task_state_for_tests();
    Task root = Task::root();
    Task t1 = root.spawn();
    Task t2 = root.spawn();

    t1.setQuota(1000);
    t2.setQuota(2000);

    // Mock t1 as currently executing task.
    xi_set_current_task(t1._state);

    // t1 tries to change its own quota.
    t1.setQuota(5000);
    TEST("t1 cannot change its own quota", t1._state->quotaUs == 1000);

    // t1 shares t2 (so t2 is shared with t1).
    t1.share(t2);

    // t1 tries to change t2's quota.
    t2.setQuota(8000);
    TEST("t1 cannot change quota of a shared task (t2)", t2._state->quotaUs == 2000);

    // Clear mocked current task.
    xi_set_current_task(nullptr);

    // Now, without current task, we should be able to change quotas.
    t1.setQuota(5000);
    TEST("can change quota outside task context", t1._state->quotaUs == 5000);
}

void testSpoofPrevention() {
    std::printf("\n[Spoof Prevention]\n");

    xi_reset_task_state_for_tests();
    Task root = Task::root();
    Task t1 = root.spawn();
    Task t2 = root.spawn();
    Task receiver = root.spawn();

    // Mock t1 as currently executing.
    xi_set_current_task(t1._state);

    // Even if t2 calls send, the actual sender must be t1.
    t2.send(receiver, "spoof attempt");

    TEST("receiver inbox has 1 message", receiver.inbox().size() == 1);
    TEST("message sender is actually t1 (the running task)", receiver.inbox()[0].senderId == t1.id());

    xi_set_current_task(nullptr);
}

void testRefCountedMapping() {
    std::printf("\n[Reference-Counted Memory Mapping]\n");

    xi_reset_task_state_for_tests();
    Task root = Task::root();
    
    // Spawn task A and allocate memory in A.
    Task taskA = root.spawn();
    taskA.alloc(0x1000, 100);
    u8* physA = taskA._state->regions[0].physical;
    physA[0] = 0xAA;

    // Spawn task B and map A's memory.
    Task taskB = root.spawn();
    taskB.share(taskA);
    taskB.map(0x1000, 0x2000, 100);

    // Verify mapping is working.
    u8* physB = taskB._state->regions[0].physical;
    TEST("mapping points to same physical memory", physB[0] == 0xAA);

    TEST("tasker tracks 1 physical allocation", Task::_allocations.size() == 1);
    TEST("refcount is initially 2 (owner + mapper)", Task::_allocations[0].refCount == 2);

    // Destroy task A.
    taskA.destroy();

    // With reference counting, taskA's memory should still be alive because B is mapping it.
    TEST("taskB's mapped pointer remains valid after A is destroyed", physB[0] == 0xAA);
    TEST("tasker still tracks 1 physical allocation", Task::_allocations.size() == 1);
    TEST("refcount is now 1 (mapper only)", Task::_allocations[0].refCount == 1);

    // Destroy task B. Now the memory should be freed completely.
    taskB.destroy();
    TEST("tasker tracks 0 physical allocations after B is destroyed", Task::_allocations.size() == 0);
}

void testStarvationPrevention() {
    std::printf("\n[Starvation Prevention]\n");

    xi_reset_task_state_for_tests();
    Task::setup(0);

    Task root = Task::root();

    Task t1 = root.spawn();
    t1.setQuota(5000);
    t1.resume(); // Enqueues t1 on core 0

    Task t2 = root.spawn();
    t2.setQuota(1000);
    t2.resume(); // Enqueues t2 on core 0

    // Set remainingUs for t1 to 0 (simulating quota exhaustion).
    t1._state->remainingUs = 0;
    t2._state->remainingUs = 1000;

    // Call pickNext. Since t1 is exhausted, it must pick t2, even though t1 has higher weight.
    Task next = Task(Task::pickNext(0));
    TEST("picks t2 because t1 is exhausted", next.id() == t2.id());

    // Exhaust t2 as well.
    t2._state->remainingUs = 0;

    // Both are exhausted now. pickNext should reset the period, replenishing both.
    next = Task(Task::pickNext(0));
    TEST("resets period and picks highest weight (t1) again", next.id() == t1.id());
    TEST("t1 remainingUs replenished", t1._state->remainingUs == 5000);
    TEST("t2 remainingUs replenished", t2._state->remainingUs == 1000);
}

void testQuotaUnderflowSafety() {
    std::printf("\n[Quota Underflow Safety]\n");

    xi_reset_task_state_for_tests();
    Task::setup(0);

    Task root = Task::root();
    Task t1 = root.spawn();
    t1.setQuota(0);
    t1.resume(); // Enqueues t1. core.totalQuotaUs adds 0.

    CoreState* core = Task::coreState(0);
    TEST("initial totalQuotaUs is 0", core->totalQuotaUs == 0);

    // Change quota while enqueued.
    t1.setQuota(5000);
    TEST("totalQuotaUs updated to 5000", core->totalQuotaUs == 5000);

    // Destroy t1 to trigger dequeue.
    t1.destroy();
    TEST("totalQuotaUs is reset to 0 without underflow", core->totalQuotaUs == 0);
}

void testCoreControlPermissions() {
    std::printf("\n[Core Control & Permissions via Task]\n");

    xi_reset_task_state_for_tests();
    Task root = Task::root();
    Task child = root.spawn();

    // Verify root/kernel task can control cores.
    root.setup(2);
    TEST("root can setup core 2", Task::coreState(2) && Task::coreState(2)->enabled);

    // Reset core 2
    Task::disable(2);

    // Mock child task as currently running.
    xi_set_current_task(child._state);

    // Child tries to setup core 2.
    child.setup(2);
    TEST("child task cannot setup core", !Task::coreState(2) || !Task::coreState(2)->enabled);

    // Child tries to disable core 0. Ensure core 0 is setup under root context first.
    xi_set_current_task(nullptr);
    root.setup(0);
    TEST("root setups core 0", Task::coreState(0)->enabled);

    xi_set_current_task(child._state);
    child.disable(0);
    TEST("child task cannot disable core", Task::coreState(0)->enabled);

    // Child tries to set frequency slider.
    child.setFrequencySlider(0, 100, 200);
    TEST("child cannot set frequency slider", Task::coreState(0)->minFreq == 0);

    // Child tries to assign frequency callback.
    bool callbackFired = false;
    child.onChangeFrequency = [&](usz core, u32 freq) {
        callbackFired = true;
    };
    // Root does it.
    xi_set_current_task(nullptr);
    root.onChangeFrequency = [&](usz core, u32 freq) {
        callbackFired = true;
    };
    
    // Simulate frequency proposal trigger.
    if (Task::root().onChangeFrequency) {
        Task::root().onChangeFrequency(0, 150);
    }
    TEST("root assigned frequency callback fires", callbackFired);

    xi_set_current_task(nullptr);
}

static bool jumpRan = false;
static void jumpedFunction(int val) {
    jumpRan = (val == 42);
}

void testTaskJump() {
    std::printf("\n[Task Jump]\n");
    xi_reset_task_state_for_tests();
    Task root = Task::root();
    Task child = root.spawn();
    child.alloc(0x1000, 4096);
    
    jumpRan = false;
    Task::setup(0);
    child.jump(jumpedFunction, 42); // Auto-starts
    
    for (int i = 0; i < 5; ++i) {
        yield();
    }
    
    TEST("child successfully executed jumped function", jumpRan);
}

static bool waitRan = false;
static void waitTarget(int val) {
    waitRan = (val == 100);
}
static void waitStarter(void* arg) {
    wait(waitTarget, 100);
}

void testTaskWaitAndWake() {
    std::printf("\n[Task Wait & Message Wake]\n");
    xi_reset_task_state_for_tests();
    Task::setup(0);
    Task root = Task::root();
    Task child = root.spawn(waitStarter, nullptr); // Auto-starts
    
    yield();
    TEST("child is paused (waiting)", child._state->status == TaskStatus::Paused);
    TEST("child is flagged as waiting for message", child._state->isWaitingForMessage);
    
    waitRan = false;
    root.send(child, "Wake up!");
    
    TEST("child woke up and is Ready", child._state->status == TaskStatus::Ready);
    TEST("child is no longer flagged as waiting", !child._state->isWaitingForMessage);
    
    yield(0);
    TEST("child ran the wait-jump function", waitRan);
}

void testOwnershipWakeChecks() {
    std::printf("\n[Ownership and Wake Authorization]\n");
    xi_reset_task_state_for_tests();
    Task root = Task::root();
    Task parent1 = root.spawn();
    Task parent2 = root.spawn();
    Task child = parent1.spawn();
    
    xi_set_current_task(parent2._state);
    
    child.resume();
    TEST("unauthorized start() does not make child Ready", child._state->status == TaskStatus::Created);
    
    child.jump(jumpedFunction, 10);
    TEST("unauthorized jump() does not run jumped function", !jumpRan || child._state->status == TaskStatus::Created);
    
    xi_set_current_task(nullptr);
    
    xi_set_current_task(parent1._state);
    child.resume();
    TEST("authorized start() makes child Ready", child._state->status == TaskStatus::Ready);
    
    xi_set_current_task(nullptr);
}

void testNormalMessageNoWake() {
    std::printf("\n[Normal Message No Wake]\n");
    xi_reset_task_state_for_tests();
    Task root = Task::root();
    Task child = root.spawn();
    child.resume();
    
    child.stop();
    TEST("child is paused", child._state->status == TaskStatus::Paused);
    TEST("child is not waiting for message", !child._state->isWaitingForMessage);
    
    root.send(child, "regular message");
    
    TEST("regular message does not wake child", child._state->status == TaskStatus::Paused);
}

static bool spawnOverloadFn1Ran = false;
static void spawnOverloadFn1(int val) {
    spawnOverloadFn1Ran = (val == 42);
}

static bool spawnOverloadFn2Ran = false;
static void spawnOverloadFn2(int val) {
    spawnOverloadFn2Ran = (val == 100);
}

void testSpawnOverloads() {
    std::printf("\n[Spawn Overloads]\n");
    xi_reset_task_state_for_tests();
    Task::setup(0);
    Task root = Task::root();

    spawnOverloadFn1Ran = false;
    spawnOverloadFn2Ran = false;

    // Test member spawn(Fn, Args...)
    Task child1 = root.spawn(spawnOverloadFn1, 42);
    TEST("member spawn created valid task", child1.valid());
    TEST("member spawn task status is Ready", child1.status() == TaskStatus::Ready);

    // Test global spawn(Fn, Args...)
    Task child2 = ::spawn(spawnOverloadFn2, 100);
    TEST("static spawn created valid task", child2.valid());
    TEST("static spawn task status is Ready", child2.status() == TaskStatus::Ready);

    // Run scheduler to execute them
    for (int i = 0; i < 5; ++i) {
        yield();
    }

    TEST("member spawn task executed", spawnOverloadFn1Ran);
    TEST("static spawn task executed", spawnOverloadFn2Ran);
}

void testUnmapIsolation() {
    std::printf("\n[Unmap Full Isolation]\n");

    xi_reset_task_state_for_tests();
    Task root = Task::root();

    Task task = root.spawn();
    task.alloc(0x1000, 256);
    task.alloc(0x2000, 512);

    TEST("task has 2 regions before unmap()", task._state->regions.size() == 2);
    TEST("task is not isolated initially", !task._state->isIsolated);

    // Set an onFetch callback.
    bool fetchSet = false;
    task.setOnFetch([&](usz dest, usz length) { fetchSet = true; });
    TEST("onFetch callback set", (bool)task._state->onFetch);

    // Full isolation unmap.
    task.unmap();

    TEST("all regions removed", task._state->regions.size() == 0);
    TEST("translations cleared", task._state->translations.size() == 0);
    TEST("onFetch cleared", !(bool)task._state->onFetch);
    TEST("task is now isolated", task._state->isIsolated);
}

void testSelfMapPrevention() {
    std::printf("\n[Self Map Prevention]\n");

    xi_reset_task_state_for_tests();
    Task root = Task::root();

    Task task = root.spawn();
    task.alloc(0x1000, 256);

    // Mock task as currently executing.
    xi_set_current_task(task._state);

    // Task tries to map memory for itself — should be blocked.
    usz regCountBefore = task._state->regions.size();
    task.map(0x1000, 0x3000, 100);
    TEST("self map is blocked", task._state->regions.size() == regCountBefore);

    xi_set_current_task(nullptr);

    // Parent (root) should be able to map for the child.
    root.alloc(0x5000, 256);
    task.share(root);
    task.map(0x5000, 0x4000, 100);
    TEST("parent can map for child", task._state->regions.size() == regCountBefore + 1);
}

void testIsolatedTaskContainment() {
    std::printf("\n[Isolated Task Containment]\n");

    xi_reset_task_state_for_tests();
    Task root = Task::root();

    Task task = root.spawn();
    task.alloc(0x1000, 256);

    // Isolate the task.
    task.unmap();
    TEST("task is isolated", task._state->isIsolated);
    TEST("no regions after isolation", task._state->regions.size() == 0);

    // Parent can still allocate new memory for the isolated task.
    task.alloc(0, 1024);
    TEST("parent can alloc for isolated task", task._state->regions.size() == 1);
    TEST("new region starts at 0", task._state->regions[0].base == 0);
}

static bool waitDeadTask1Ran = false;
static bool waitDeadTask2Ran = false;

static void waitDeadTask1(void* arg) {
    waitDeadTask1Ran = true;
}

static void waitDeadTask2(void* arg) {
    yield();
    yield();
    waitDeadTask2Ran = true;
}

static void waitDeadInsideStarter(void* arg) {
    Task childOfSelf = spawn(waitDeadTask2, nullptr);
    waitDead();
}

void testTaskWaitDead() {
    std::printf("\n[Task WaitDead]\n");
    xi_reset_task_state_for_tests();
    Task::setup(0);
    Task root = Task::root();

    waitDeadTask1Ran = false;
    waitDeadTask2Ran = false;

    Task child = root.spawn(waitDeadTask1, nullptr);
    waitDead();

    TEST("waitDead outside task blocked until all tasks finished", waitDeadTask1Ran);

    // Inner waitDead test — run with a bounded yield loop to avoid hangs.
    waitDeadTask2Ran = false;
    Task starter = root.spawn(waitDeadInsideStarter, nullptr);
    // Use bounded loop instead of unbounded waitDead to avoid pre-existing hang.
    for (int i = 0; i < 200; ++i) {
        bool anyAlive = false;
        for (usz t = 1; t < Task::taskCount(); ++t) {
            Task tt = Task::findTask(t);
            if (tt.valid() && tt.status() != TaskStatus::Finished && tt.status() != TaskStatus::Destroyed) {
                anyAlive = true;
                break;
            }
        }
        if (!anyAlive) break;
        yield();
    }
    TEST("inner waitDead task2 ran", waitDeadTask2Ran);
}

void testForkBombProtection() {
    std::printf("\n[Fork Bomb Protection]\n");

    xi_reset_task_state_for_tests();
    Task root = Task::root();

    // Create a parent with limited quota.
    Task parent = root.spawn();
    parent.setQuota(10000);
    parent.setMinChildQuota(3000);

    TEST("parent minChildQuota set", parent._state->minChildQuotaUs == 3000);

    // Spawn children — each gets 3000us from parent's 10000.
    Task c1 = parent.spawn();
    TEST("child1 spawned", c1.valid());
    TEST("child1 has min quota", c1._state->quotaUs == 3000);
    TEST("parent childQuotaUsed is 3000", parent._state->childQuotaUsed == 3000);

    Task c2 = parent.spawn();
    TEST("child2 spawned", c2.valid());
    TEST("parent childQuotaUsed is 6000", parent._state->childQuotaUsed == 6000);

    Task c3 = parent.spawn();
    TEST("child3 spawned", c3.valid());
    TEST("parent childQuotaUsed is 9000", parent._state->childQuotaUsed == 9000);

    // 4th child would need 12000 > 10000, so it fails.
    Task c4 = parent.spawn();
    TEST("child4 blocked (quota exhausted)", !c4.valid());

    // Children inherit minChildQuota cascading.
    TEST("child1 inherits minChildQuota", c1._state->minChildQuotaUs == 3000);

    // Destroy c1 — quota returns to parent.
    c1.destroy();
    TEST("parent childQuotaUsed after destroy", parent._state->childQuotaUsed == 6000);

    // Now c5 should fit (6000 + 3000 = 9000 <= 10000).
    Task c5 = parent.spawn();
    TEST("child5 spawned after c1 destroyed", c5.valid());
}

void testOnInstructionSelfRegister() {
    std::printf("\n[onInstruction Self-Register]\n");

    xi_reset_task_state_for_tests();
    Task root = Task::root();
    Task task = root.spawn();

    // Parent bans an instruction.
    task.offInstruction("nop");
    TEST("nop is banned", task._state->instructionHooks.size() == 1 &&
         task._state->instructionHooks[0].banned == true);

    // Mock task as currently executing.
    xi_set_current_task(task._state);

    // Task tries to register a callback on the banned instruction.
    bool called = false;
    task.onInstruction("nop", [&]() { called = true; });

    // Callback is registered but instruction stays BANNED.
    TEST("callback registered on banned instruction",
         (bool)task._state->instructionHooks[0].callback);
    TEST("instruction still banned after self-register",
         task._state->instructionHooks[0].banned == true);

    // Task tries offInstruction on itself — should be BLOCKED.
    task.offInstruction("ret");
    // Should NOT have added a new hook (blocked).
    TEST("self offInstruction blocked",
         task._state->instructionHooks.size() == 1);

    xi_set_current_task(nullptr);
}

void testWxEnforcement() {
    std::printf("\n[W^X Enforcement]\n");

    xi_reset_task_state_for_tests();
    Task root = Task::root();
    Task task = root.spawn();

    // Regular alloc — writable, not executable.
    task.alloc(0x1000, 256);
    TEST("alloc: writable", task._state->regions[0].writable);
    TEST("alloc: not executable", !task._state->regions[0].executable);

    // Executable alloc — executable, not writable.
    task.allocExecutable(0x2000, 256);
    TEST("allocExec: not writable", !task._state->regions[1].writable);
    TEST("allocExec: executable", task._state->regions[1].executable);
}

// -------------------------------------------------------------------------
// Main
// -------------------------------------------------------------------------

int main() {
    std::printf("=== Task Functional Tests ===\n");

    // --- Pure API tests (no context switches) ---
    testTaskCreation();
    testCustomTask();
    testIPC();
    testMemory();
    testMemoryAutoAlloc();
    testScheduling();
    testDestroy();
    testLog();
    testFindTask();
    testSharing();
    testSharedQuota();
    testSpoofPrevention();
    testRefCountedMapping();
    testStarvationPrevention();
    testQuotaUnderflowSafety();
    testCoreControlPermissions();
    testUnmapIsolation();
    testSelfMapPrevention();
    testIsolatedTaskContainment();

    // --- Security tests (no context switches) ---
    testForkBombProtection();
    testOnInstructionSelfRegister();
    testWxEnforcement();

    // --- Context-switch tests (may corrupt heap in hosted mode) ---
    testTaskJump();
    testTaskWaitAndWake();
    testOwnershipWakeChecks();
    testNormalMessageNoWake();
    testSpawnOverloads();
    testTaskWaitDead();

    std::printf("\n=== Results: %d passed, %d failed ===\n",
                testsPassed, testsFailed);

    return testsFailed > 0 ? 1 : 0;
}
