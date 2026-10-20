#pragma once

#include <functional>
#include <utility>

namespace mviewer::core
{

// Thread-local cooperative cancel probe for long, otherwise non-interruptible
// library work that cannot take a token through a frozen interface (IDecoder /
// DecoderRegistry): LibRaw's dcraw_process() polls it from its progress
// callback.
//
// The code that owns a request (ImageRepository's async worker, a scheduler
// task with a TaskContext) installs a probe for the duration of the work:
//
//     ScopedCancelProbe probe([state] { return state->cancelled.load(); });
//     decode(...); // deep inside: if (cooperativeCancelRequested()) abort
//
// Probes nest; any enclosing probe that reports stale cancels. A probe must be
// cheap, non-blocking and thread-safe: it runs on the decoding thread and may
// also be polled by a watcher thread for the lifetime of the scope. Without an installed probe
// nothing is ever cancelled (synchronous callers keep their exact behaviour).
class ScopedCancelProbe
{
  public:
    explicit ScopedCancelProbe(std::function<bool()> probe)
        : m_probe(std::move(probe)), m_previous(current())
    {
        current() = this;
    }
    ~ScopedCancelProbe()
    {
        current() = m_previous;
    }
    ScopedCancelProbe(const ScopedCancelProbe &) = delete;
    ScopedCancelProbe &operator=(const ScopedCancelProbe &) = delete;

    static bool cancelRequested()
    {
        return cancelRequested(current());
    }

    // The probe chain installed on the calling thread. A helper thread (e.g.
    // a LibRaw cancel watcher) may evaluate it with cancelRequested(chain)
    // while the owning scope is alive; probes must therefore be thread-safe.
    static const ScopedCancelProbe *installed()
    {
        return current();
    }

    static bool cancelRequested(const ScopedCancelProbe *chain)
    {
        for (const ScopedCancelProbe *p = chain; p; p = p->m_previous)
        {
            if (p->reportsStale())
                return true;
        }
        return false;
    }

  private:
    bool reportsStale() const
    {
        try
        {
            return m_probe && m_probe();
        }
        catch (...)
        {
            // A throwing probe cannot decide; treat it as "keep going".
            return false;
        }
    }

    static const ScopedCancelProbe *&current()
    {
        static thread_local const ScopedCancelProbe *top = nullptr;
        return top;
    }

    std::function<bool()> m_probe;
    const ScopedCancelProbe *m_previous;
};

inline bool cooperativeCancelRequested()
{
    return ScopedCancelProbe::cancelRequested();
}

} // namespace mviewer::core
