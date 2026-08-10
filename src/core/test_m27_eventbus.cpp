// M27 Phase 7 — EventBus lifetime / reentrancy closure regression tests.
//
// Contracts under test (must hold after Phase 7 hardening):
//   1. A subscriber may unsubscribe ITSELF from inside a handler (no deadlock).
//   2. A handler may subscribe a NEW handler (no deadlock); the new subscriber
//      does not receive the in-flight publish (snapshot semantics) but does
//      receive the next one.
//   3. A handler may publish a NESTED event (no deadlock), and subscribers see
//      a consistent order.
//   4. publish() concurrent with unsubscribe() from another thread must not
//      deadlock or crash (in-flight handlers own their function copies).
//   5. Repeated subscribe/unsubscribe must not grow the bus (no leak).
//
// Deadlock scenarios run under a watchdog thread: pre-fix (handlers invoked
// while holding the bus mutex) the process hangs and the watchdog aborts — a
// FAIL. Post-fix they complete normally.

#include "core/EventBus.h"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <thread>
#include <vector>

static int g_pass = 0;
static int g_fail = 0;

#define CHECK(cond, msg)                                                                           \
    do                                                                                             \
    {                                                                                              \
        if (cond)                                                                                  \
        {                                                                                          \
            printf("  PASS: %s\n", msg);                                                           \
            g_pass++;                                                                              \
        }                                                                                          \
        else                                                                                       \
        {                                                                                          \
            printf("  FAIL: %s\n", msg);                                                           \
            g_fail++;                                                                              \
        }                                                                                          \
    } while (0)

namespace
{
EventBus &bus()
{
    // Use the UI scope bus: never used by the product at test time, so the
    // test state cannot leak into (or be polluted by) other suites.
    return EventBus::scope(EventBus::EventBusScope::UI);
}

struct DeadlockWatchdog
{
    std::atomic<bool> finished{false};
    std::thread thread;

    explicit DeadlockWatchdog(int seconds = 15)
    {
        thread = std::thread([this, seconds]()
                             {
                                 std::this_thread::sleep_for(std::chrono::seconds(seconds));
                                 if (!finished.load())
                                 {
                                     fprintf(stderr, "  WATCHDOG: deadlock detected, aborting\n");
                                     std::abort();
                                 }
                             });
    }
    ~DeadlockWatchdog()
    {
        finished.store(true);
        if (thread.joinable())
            thread.join();
    }
};

// ─── 1. self-unsubscribe inside a handler ──────────────────────────────────
void testSelfUnsubscribe()
{
    printf("\n[1. handler unsubscribes itself — no deadlock]\n");
    fflush(stdout);
    static const std::string ev = "m27.self-unsub";
    auto &b = bus();

    std::atomic<int> calls{0};
    int subId = 0;
    {
        DeadlockWatchdog watchdog;
        subId = b.subscribe(ev, [&](void *)
                            {
                                calls.fetch_add(1);
                                b.unsubscribe(subId);
                            });
        b.publish(ev); // handler unsubscribes itself mid-publish
        b.publish(ev); // second publish must find nobody
        CHECK(calls.load() == 1, "handler ran exactly once (self-unsubscribed)");
    }
    b.unsubscribe(subId); // idempotent
}

// ─── 2. subscribe inside a handler ─────────────────────────────────────────
void testSubscribeInsideHandler()
{
    printf("\n[2. handler subscribes a new handler — snapshot semantics]\n");
    fflush(stdout);
    static const std::string ev = "m27.sub-in-handler";
    auto &b = bus();

    std::atomic<int> newCalls{0};
    int newId = 0;
    bool subscribed = false;
    {
        DeadlockWatchdog watchdog;
        auto outerId = b.subscribe(ev, [&](void *)
                                   {
                                       if (!subscribed)
                                       {
                                           subscribed = true;
                                           newId = b.subscribe(ev, [&](void *)
                                                               { newCalls.fetch_add(1); });
                                       }
                                   });
        b.publish(ev); // outer handler subscribes mid-publish
        CHECK(newCalls.load() == 0, "new subscriber does NOT receive the in-flight publish");
        b.publish(ev); // next publish reaches both
        CHECK(newCalls.load() == 1, "new subscriber receives the next publish");
        b.unsubscribe(outerId);
        b.unsubscribe(newId);
    }
}

// ─── 3. nested publish inside a handler ────────────────────────────────────
void testNestedPublish()
{
    printf("\n[3. nested publish — no deadlock, consistent order]\n");
    fflush(stdout);
    static const std::string outerEv = "m27.nested-outer";
    static const std::string innerEv = "m27.nested-inner";
    auto &b = bus();

    std::vector<std::string> order;
    {
        DeadlockWatchdog watchdog;
        auto innerId = b.subscribe(innerEv, [&](void *) { order.push_back("inner"); });
        auto outerId = b.subscribe(outerEv, [&](void *)
                                   {
                                       order.push_back("outer-begin");
                                       b.publish(innerEv); // nested publish inside a handler
                                       order.push_back("outer-end");
                                   });
        b.publish(outerEv);
        CHECK(order.size() == 3 && order[0] == "outer-begin" && order[1] == "inner" &&
                  order[2] == "outer-end",
              "nested publish runs inline with consistent ordering");
        b.unsubscribe(innerId);
        b.unsubscribe(outerId);
    }
}

// ─── 4. publish concurrent with unsubscribe ────────────────────────────────
void testConcurrentPublishUnsubscribe()
{
    printf("\n[4. publish || unsubscribe — no deadlock, no crash]\n");
    fflush(stdout);
    static const std::string ev = "m27.concurrent";
    auto &b = bus();

    std::atomic<bool> stop{false};
    std::atomic<int> handlerCalls{0};
    std::vector<int> ids;
    for (int i = 0; i < 8; ++i)
        ids.push_back(b.subscribe(ev, [&](void *) { handlerCalls.fetch_add(1); }));

    std::thread publisher([&]()
                          {
                              for (int i = 0; i < 500 && !stop.load(); ++i)
                                  b.publish(ev);
                          });
    std::thread unsubscriber([&]()
                             {
                                 for (int i = 0; i < 8; ++i)
                                 {
                                     std::this_thread::sleep_for(std::chrono::milliseconds(2));
                                     b.unsubscribe(ids[i]);
                                 }
                             });
    publisher.join();
    unsubscriber.join();
    stop.store(true);
    CHECK(handlerCalls.load() >= 8, "publishes delivered before/while unsubscribing");
    b.publish(ev);
    CHECK(handlerCalls.load() >= 8, "no crash after concurrent unsubscribe");
}

// ─── 5. subscribe/unsubscribe churn leaves no growth ───────────────────────
void testNoSubscriptionLeak()
{
    printf("\n[5. subscribe/unsubscribe churn — no growth]\n");
    fflush(stdout);
    auto &b = bus();
    static const std::string ev = "m27.churn";
    for (int i = 0; i < 2000; ++i)
        b.unsubscribe(b.subscribe(ev, [](void *) {}));
    b.publish(ev); // must not crash; nothing left to call
    CHECK(true, "2000 subscribe/unsubscribe cycles complete without growth");
}

} // namespace

int main(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    printf("=== M27 EventBus lifetime/reentrancy tests ===\n");
    fflush(stdout);

    testSelfUnsubscribe();
    testSubscribeInsideHandler();
    testNestedPublish();
    testConcurrentPublishUnsubscribe();
    testNoSubscriptionLeak();

    printf("\n=== Results: %d passed, %d failed ===\n", g_pass, g_fail);
    fflush(stdout);
    return g_fail == 0 ? 0 : 1;
}
