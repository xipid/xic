/**
 * @file test_tasker.cpp
 * @brief Functional test for the Tasker subsystem.
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
#include "Execution/Tasker.hpp"

using namespace Execution;
using namespace Xi;
using namespace Collection;

// -------------------------------------------------------------------------
// CustomTask subclass test — must work with Tasker seamlessly.
// -------------------------------------------------------------------------

struct CustomTask : public Task {
    int permissions;
    String role;

    CustomTask() : Task(), permissions(0) {}
    CustomTask(TaskState* state, Tasker* tasker)
        : Task(state, tasker), permissions(0) {}

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

    Tasker tasker;
    Task root = tasker.root();

    TEST("root is valid", root.valid());
    TEST("root id is 0", root.id() == 0);
    TEST("root has no parent (parentId == 0)", root.parentId() == 0);
    TEST("taskCount starts at 1", tasker.taskCount() == 1);

    Task child = root.spawn();
    TEST("child is valid", child.valid());
    TEST("child id is 1", child.id() == 1);
    TEST("child parentId is root", child.parentId() == 0);
    TEST("taskCount is 2", tasker.taskCount() == 2);

    Task grandchild = child.spawn();
    TEST("grandchild is valid", grandchild.valid());
    TEST("grandchild parentId is child", grandchild.parentId() == 1);
    TEST("taskCount is 3", tasker.taskCount() == 3);

    TEST("root has 1 child", root.childCount() == 1);
    TEST("child has 1 child", child.childCount() == 1);
}

void testCustomTask() {
    std::printf("\n[CustomTask Subclassing]\n");

    Tasker tasker;
    Task root = tasker.root();

    // Create a CustomTask from a spawned task.
    Task base = root.spawn();
    CustomTask ct;
    ct._state = base._state;
    ct._tasker = base._tasker;
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

    Tasker tasker;
    Task root = tasker.root();
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

    Tasker tasker;
    Task root = tasker.root();
    Task task = root.spawn();

    // Auto-alloc happens on start(), but we can manually alloc.
    task.alloc(0x1000, 256);
    TEST("task has 1 region after alloc", task._state->regions.size() == 1);
    TEST("region base is 0x1000", task._state->regions[0].base == 0x1000);
    TEST("region size is 256", task._state->regions[0].size == 256);
    TEST("region is writable", task._state->regions[0].writable);
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

    Tasker tasker;
    Task root = tasker.root();
    Task task = root.spawn();

    // Don't call alloc — start() should auto-allocate.
    task.setEntry([](void*) {}, nullptr);
    // Can't actually run (would need scheduler), but start() should
    // allocate memory and context.
    task.start();

    TEST("auto-alloc created region", task._state->regions.size() > 0);
    TEST("auto-alloc size is 4KB", task._state->regions[0].size == 4096);
    TEST("stack was allocated", task._state->stack != nullptr);
    TEST("task is Ready", task.status() == TaskStatus::Ready);
}

void testScheduling() {
    std::printf("\n[Scheduling]\n");

    Tasker tasker;
    Task root = tasker.root();

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

    Tasker tasker;
    Task root = tasker.root();

    Task parent = root.spawn();
    Task child1 = parent.spawn();
    Task child2 = parent.spawn();
    Task grandchild = child1.spawn();

    usz parentId = parent.id();
    usz child1Id = child1.id();
    usz grandchildId = grandchild.id();

    TEST("4 tasks before destroy", tasker.taskCount() == 5); // root + 4

    // Destroy parent — should cascade to children and grandchildren.
    parent.destroy();

    TEST("parent destroyed", tasker.findTask(parentId).valid() == false);
    TEST("child1 destroyed", tasker.findTask(child1Id).valid() == false);
    TEST("grandchild destroyed", tasker.findTask(grandchildId).valid() == false);
}

void testLog() {
    std::printf("\n[Log]\n");

    Tasker tasker;
    Task root = tasker.root();
    Task task = root.spawn();

    task.log().push("output line 1");
    task.log().push("output line 2");

    TEST("log has 2 entries", task.log().size() == 2);
    TEST("log[0] correct", task.log()[0] == "output line 1");
    TEST("log[1] correct", task.log()[1] == "output line 2");
}

void testFindTask() {
    std::printf("\n[FindTask]\n");

    Tasker tasker;
    Task root = tasker.root();

    Task t1 = root.spawn();
    Task t2 = root.spawn();
    Task t3 = root.spawn();

    TEST("find root", tasker.findTask(0).valid());
    TEST("find t1", tasker.findTask(t1.id()).id() == t1.id());
    TEST("find t2", tasker.findTask(t2.id()).id() == t2.id());
    TEST("find t3", tasker.findTask(t3.id()).id() == t3.id());
    TEST("invalid id returns invalid", !tasker.findTask(999).valid());
}

void testSharing() {
    std::printf("\n[Sharing]\n");

    Tasker tasker;
    Task root = tasker.root();

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

// -------------------------------------------------------------------------
// Main
// -------------------------------------------------------------------------

int main() {
    std::printf("=== Tasker Functional Tests ===\n");

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

    std::printf("\n=== Results: %d passed, %d failed ===\n",
                testsPassed, testsFailed);

    return testsFailed > 0 ? 1 : 0;
}
