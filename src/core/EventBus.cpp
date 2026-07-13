#include "core/EventBus.h"

#include <algorithm>

EventBus &EventBus::instance()
{
    static EventBus inst;
    return inst;
}

EventBus &EventBus::scope(EventBusScope s)
{
    static EventBus buses[4];
    return buses[static_cast<int>(s)];
}

int EventBus::subscribe(const std::string &event, Handler h)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    int id = m_nextId++;
    m_subs[event].push_back({id, std::move(h)});
    return id;
}

void EventBus::unsubscribe(int id)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    for (auto &it : m_subs) {
        auto &vec = it.second;
        vec.erase(std::remove_if(vec.begin(), vec.end(),
                                 [id](const Subscriber &s) { return s.id == id; }),
                  vec.end());
    }
}

void EventBus::publish(const std::string &event, void *ctx)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_subs.find(event);
    if (it == m_subs.end()) return;
    for (const auto &sub : it->second)
        if (sub.h) sub.h(ctx);
}
