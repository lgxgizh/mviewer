#pragma once

#include <atomic>
#include <memory>

// M46: consumer lifetime token for cancellable async requests.
//
// Contract (enforced by ImageRepository / ImageLoadingFacade):
//   * the consumer owns a shared_ptr<AsyncLifetimeToken> and calls
//     invalidate() from its destructor (UI thread);
//   * the request layer stores a weak_ptr to that token and checks it at the
//     terminal delivery gate, so a request whose consumer was destroyed can
//     never start a NEW client callback — the late completion becomes a safe
//     no-op before any consumer-visible code runs;
//   * cancellation (cancelAsync) waits for an already-started delivery to
//     finish, so after cancelAsync() returns no client callback is running or
//     will start for that request.
//
// Header is Qt-free (core layer contract).

namespace mviewer::core
{

class AsyncLifetimeToken
{
  public:
    AsyncLifetimeToken() = default;
    ~AsyncLifetimeToken() = default;
    AsyncLifetimeToken(const AsyncLifetimeToken &) = delete;
    AsyncLifetimeToken &operator=(const AsyncLifetimeToken &) = delete;

    bool isAlive() const
    {
        return m_alive.load(std::memory_order_acquire);
    }

    // Called by the owning consumer (normally from its destructor, on the
    // thread that owns it). After this returns, every request that carries a
    // weak_ptr to this token suppresses its client delivery.
    void invalidate()
    {
        m_alive.store(false, std::memory_order_release);
    }

    static std::shared_ptr<AsyncLifetimeToken> create()
    {
        return std::make_shared<AsyncLifetimeToken>();
    }

  private:
    std::atomic<bool> m_alive{true};
};

} // namespace mviewer::core
