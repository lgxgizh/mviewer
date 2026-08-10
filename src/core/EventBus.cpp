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
    for (auto &it : m_subs)
    {
        auto &vec = it.second;
        vec.erase(std::remove_if(vec.begin(), vec.end(),
                                 [id](const Subscriber &s) { return s.id == id; }),
                  vec.end());
    }
}

void EventBus::publish(const std::string &event, void *ctx)
{
    // M27: handlers run OUTSIDE the lock. The lock only snapshots the
    // subscriber list; user code (subscribe / unsubscribe / nested publish /
    // destruction) can then run freely inside a handler without deadlocking.
    // Each in-flight handler holds its own copy of the std::function, so a
    // concurrent unsubscribe() cannot destroy a handler that is executing.
    // NOTE: snapshot semantics — a subscriber that unsubscribes during the
    // same publish may still receive that publish (its entry was already
    // copied). Consumers that capture raw pointers must guard them (QPointer
    // etc.) exactly because of this delivery-vs-destruction race.
    std::vector<Handler> snapshot;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        auto it = m_subs.find(event);
        if (it == m_subs.end())
            return;
        snapshot.reserve(it->second.size());
        for (const auto &sub : it->second)
            snapshot.push_back(sub.h);
    }
    for (const auto &h : snapshot)
    {
        if (h)
            h(ctx);
    }
}
