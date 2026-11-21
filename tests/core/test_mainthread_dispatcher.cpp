// Main-thread delivery: after shutdown, a late worker's callback must be
// dropped rather than executed inline on the worker thread (which is what the
// old "clear the dispatcher" path did at Shutdown.cpp).
#include "core/MainThreadDispatcher.h"

#include <cstdio>
#include <functional>

namespace
{
int g_pass = 0;
int g_fail = 0;

#define CHECK(cond, msg)                                                                           \
    do                                                                                             \
    {                                                                                              \
        if (cond)                                                                                  \
        {                                                                                          \
            printf("  PASS: %s\n", msg);                                                           \
            ++g_pass;                                                                              \
        }                                                                                          \
        else                                                                                       \
        {                                                                                          \
            printf("  FAIL: %s\n", msg);                                                           \
            ++g_fail;                                                                              \
        }                                                                                          \
    } while (false)
} // namespace

int main()
{
    setbuf(stdout, nullptr);
    using mvcore::postToMainThread;
    using mvcore::setMainThreadDispatcher;
    using mvcore::shutdownMainThreadDispatcher;

    int dispatched = 0;
    int executed = 0;
    setMainThreadDispatcher(
        [&](std::function<void()> fn)
        {
            ++dispatched;
            fn();
        });
    postToMainThread([&] { ++executed; });
    CHECK(dispatched == 1 && executed == 1, "a posted closure goes through the dispatcher");

    // No dispatcher at all: the calling thread owns the callback.
    setMainThreadDispatcher({});
    postToMainThread([&] { ++executed; });
    CHECK(dispatched == 1 && executed == 2, "without a dispatcher the closure runs inline");

    shutdownMainThreadDispatcher();
    postToMainThread([&] { ++executed; });
    CHECK(dispatched == 1 && executed == 2,
          "after shutdown a late closure is dropped, never run on the worker");

    printf("\n==== main thread dispatcher test: %d passed, %d failed ====\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
