#include <Resource/Router.hpp>
#include <cstdio>
#include <cassert>

using namespace Resource;
using namespace Xi;
using namespace Collection;

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

void testBasicRoutingAndChaining() {
    std::printf("\n[Basic Routing & Chaining]\n");
    Router rtr;
    
    // Test fluent chaining
    bool mainTriggered = false;
    rtr.get("/test", [&](Router& r, Path& p, void* ctx) -> Path {
        mainTriggered = true;
        return p;
    }).get("/other", [&](Router& r, Path& p, void* ctx) -> Path {
        return p;
    });

    Path req("get://localhost/test");
    Path res = rtr.resolve(req);
    TEST("Main route resolved and executed", mainTriggered);
}

void testMiddlewareCascadingOrder() {
    std::printf("\n[Middleware Cascading & Order]\n");
    Router rtr;
    Array<String> order;

    // Direct callback overloads, fluent chaining, and phases
    rtr.before().on([&](Router& r, Path& p, void* ctx) -> Path {
        order.push("before1_root");
        return p;
    }).on("/admin", [&](Router& r, Path& p, void* ctx) -> Path {
        order.push("before2_admin");
        return p;
    });

    rtr.on("/admin/users", [&](Router& r, Path& p, void* ctx) -> Path {
        order.push("main");
        return p;
    });

    rtr.after().on("/admin", [&](Router& r, Path& p, void* ctx) -> Path {
        order.push("after_admin");
        return p;
    });

    rtr.ready().on([&](Router& r, Path& p, void* ctx) -> Path {
        order.push("ready_root");
        return p;
    });

    Path req("get://localhost/admin/users");
    rtr.resolve(req);

    TEST("Cascaded execution size is 5", order.size() == 5);
    if (order.size() == 5) {
        TEST("First is before1_root (depth 0)", order[0] == "before1_root");
        TEST("Second is before2_admin (depth 1)", order[1] == "before2_admin");
        TEST("Third is main", order[2] == "main");
        TEST("Fourth is after_admin", order[3] == "after_admin");
        TEST("Fifth is ready_root", order[4] == "ready_root");
    }
}

void testHaltControlPath() {
    std::printf("\n[Halt Control Path]\n");
    
    // Test 1: Halt in Before phase
    {
        Router rtr;
        Array<String> order;

        rtr.before().on([&](Router& r, Path& p, void* ctx) -> Path {
            order.push("before_1");
            return p;
        }).on([&](Router& r, Path& p, void* ctx) -> Path {
            order.push("before_2_halt");
            return Path("halt"); // Resolves to halt control path
        }).on([&](Router& r, Path& p, void* ctx) -> Path {
            order.push("before_3");
            return p;
        });

        rtr.on("/test", [&](Router& r, Path& p, void* ctx) -> Path {
            order.push("main");
            return p;
        });

        Path req("get://localhost/test");
        Path res = rtr.resolve(req);

        TEST("Resolution results in a halt path", res.isHalt());
        TEST("Execution stopped at halt", order.size() == 2);
        if (order.size() == 2) {
            TEST("First is before_1", order[0] == "before_1");
            TEST("Second is before_2_halt", order[1] == "before_2_halt");
        }
    }

    // Test 2: Halt in Main phase
    {
        Router rtr;
        Array<String> order;

        rtr.before().on([&](Router& r, Path& p, void* ctx) -> Path {
            order.push("before");
            return p;
        });

        rtr.on("/test", [&](Router& r, Path& p, void* ctx) -> Path {
            order.push("main_halt");
            return Path("halt");
        });

        rtr.after().on([&](Router& r, Path& p, void* ctx) -> Path {
            order.push("after");
            return p;
        });

        Path req("get://localhost/test");
        Path res = rtr.resolve(req);

        TEST("Resolution results in a halt path", res.isHalt());
        TEST("Execution stopped at main", order.size() == 2);
        if (order.size() == 2) {
            TEST("First is before", order[0] == "before");
            TEST("Second is main_halt", order[1] == "main_halt");
        }
    }
}

void testRouteRemoval() {
    std::printf("\n[Route Removal via .off()]\n");
    Router rtr;
    int triggerCount = 0;

    rtr.get("/target", [&](Router& r, Path& p, void* ctx) -> Path {
        triggerCount++;
        return p;
    });

    Path req("get://localhost/target");
    rtr.resolve(req);
    TEST("Route triggered once", triggerCount == 1);

    // Remove route
    rtr.off("GET", "/target");
    
    rtr.resolve(req);
    TEST("Route not triggered after removal", triggerCount == 1);
}

int main() {
    std::printf("=========================================\n");
    std::printf("   Router Feature Tests                  \n");
    std::printf("=========================================\n");

    testBasicRoutingAndChaining();
    testMiddlewareCascadingOrder();
    testHaltControlPath();
    testRouteRemoval();

    std::printf("\n=========================================\n");
    std::printf("  Passed: %d, Failed: %d\n", testsPassed, testsFailed);
    std::printf("=========================================\n");

    return testsFailed > 0 ? 1 : 0;
}
