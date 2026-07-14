#pragma once

#include <functional>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

// Tiny type-erased event bus. Subscribe with a callback; publish synchronously.
// No Qt dependency.
class EventBus {
public:
  // Logical scopes — each scope is an isolated bus instance so events in one
  // domain (e.g. Analysis) don't cross-fire subscribers in another (e.g. UI).
  enum class EventBusScope : uint8_t { Application, Image, Analysis, UI };

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
  struct Subscriber {
    int id;
    Handler h;
  };
  int m_nextId = 1;
  std::unordered_map<std::string, std::vector<Subscriber>> m_subs;
  std::mutex m_mutex;
};
