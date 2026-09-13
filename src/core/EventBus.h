#pragma once

#include <functional>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

// Tiny type-erased event bus. Subscribe with a callback; publish synchronously.
// No Qt dependency.
//
// STATUS (2026-08 review): no production path publishes or subscribes today.
// Async results are delivered through the transports of the code that produces
// them — CompareWorkspace marshals its diff/materialization batches to qApp with
// a QPointer + generation guard, ImageRepository delivers through result
// callbacks, and UI signals stay plain Qt signals. The bus is retained as tested
// infrastructure (core/test_eventbus.cpp, core/test_m27_eventbus.cpp) for
// genuinely decoupled, multi-subscriber notifications; do NOT add a publisher
// that has no production subscriber, and do not use it as a single-consumer
// completion channel — use a callback for that.
class EventBus
{
  public:
    // Logical scopes — each scope is an isolated bus instance so events in one
    // domain (e.g. Analysis) don't cross-fire subscribers in another (e.g. UI).
    enum class EventBusScope : uint8_t
    {
        Application,
        Image,
        Analysis,
        UI
    };

    static EventBus &instance();
    static EventBus &scope(EventBusScope s);

    // Subscribe to an event type. Returns subscription id (use to unsubscribe).
    using Handler = std::function<void(void *)>;
    int subscribe(const std::string &event, Handler h);
    void unsubscribe(int id);

    // Publish an event (synchronous call to all subscribers).
    // ctx is opaque user data (e.g., pointer to event data struct).
    void publish(const std::string &event, void *ctx = nullptr);

  private:
    struct Subscriber
    {
        int id;
        Handler h;
    };
    int m_nextId = 1;
    std::unordered_map<std::string, std::vector<Subscriber>> m_subs;
    std::mutex m_mutex;
};
