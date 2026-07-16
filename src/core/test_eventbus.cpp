// M7 unit tests: EventBus (review ② — previously uncovered easy-regression area).
#include "core/EventBus.h"

#include <QCoreApplication>
#include <cstdio>
#include <string>

static int g_pass = 0;
static int g_fail = 0;

#define CHECK(cond, msg)                 \
    do                                   \
    {                                    \
        if (cond)                        \
        {                                \
            ++g_pass;                    \
        }                                \
        else                             \
        {                                \
            ++g_fail;                    \
            printf("  FAIL: %s\n", msg); \
        }                                \
    } while (0)

static void testEventBusPublishSubscribe()
{
    printf("\n[EventBusPublishSubscribe]\n");
    EventBus& bus = EventBus::instance();
    bus.unsubscribe(0); // no-op safety

    int deliveries = 0;
    void* lastCtx = nullptr;
    int id = bus.subscribe("ImageLoaded", [&](void* ctx) {
        ++deliveries;
        lastCtx = ctx;
    });
    CHECK(id != 0, "subscribe returns non-zero id");

    int other = 0;
    bus.subscribe("DirectoryChanged", [&](void*) { ++other; });

    int marker = 42;
    bus.publish("ImageLoaded", &marker);
    CHECK(deliveries == 1, "handler invoked on publish");
    CHECK(lastCtx == &marker, "context pointer delivered unchanged");

    bus.publish("DirectoryChanged");
    CHECK(other == 1, "other event delivered to its subscriber only");
    CHECK(deliveries == 1, "ImageLoaded subscriber not double-fired by unrelated event");
}

static void testEventBusUnsubscribe()
{
    printf("\n[EventBusUnsubscribe]\n");
    EventBus& bus = EventBus::instance();

    int deliveries = 0;
    int id = bus.subscribe("ThumbnailReady", [&](void*) { ++deliveries; });
    bus.publish("ThumbnailReady");
    CHECK(deliveries == 1, "handler fires before unsubscribe");

    bus.unsubscribe(id);
    bus.publish("ThumbnailReady");
    CHECK(deliveries == 1, "no delivery after unsubscribe");
}

static void testEventBusScopeIsolation()
{
    printf("\n[EventBusScopeIsolation]\n");
    EventBus& ui = EventBus::scope(EventBus::EventBusScope::UI);
    EventBus& core = EventBus::scope(EventBus::EventBusScope::Analysis);

    int uiHits = 0;
    int coreHits = 0;
    ui.subscribe("SharedEvent", [&](void*) { ++uiHits; });
    core.subscribe("SharedEvent", [&](void*) { ++coreHits; });

    ui.publish("SharedEvent");
    CHECK(uiHits == 1, "UI scope receives its own event");
    CHECK(coreHits == 0, "Analysis scope NOT cross-fired by UI publish");

    core.publish("SharedEvent");
    CHECK(coreHits == 1, "Analysis scope receives its own event");
    CHECK(uiHits == 1, "UI scope still isolated after Analysis publish");
}

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    printf("=== EventBus Tests (M7) ===\n");
    fflush(stdout);

    testEventBusPublishSubscribe();
    testEventBusUnsubscribe();
    testEventBusScopeIsolation();

    printf("\n=== Results: %d passed, %d failed ===\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
