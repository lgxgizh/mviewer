#include "core/MainThreadDispatcher.h"

#include <mutex>
#include <utility>

namespace
{

// Leaked singletons: a core producer can deliver during static destruction
// (thread pools draining at exit), so the storage must outlive every
// translation unit's static destructors.
std::mutex &dispatcherMutex()
{
    static auto *mutex = new std::mutex();
    return *mutex;
}

mvcore::MainThreadDispatch &dispatcherStorage()
{
    static auto *dispatch = new mvcore::MainThreadDispatch();
    return *dispatch;
}

} // namespace

namespace mvcore
{

void setMainThreadDispatcher(MainThreadDispatch dispatch)
{
    std::lock_guard<std::mutex> lock(dispatcherMutex());
    dispatcherStorage() = std::move(dispatch);
}

bool hasMainThreadDispatcher()
{
    std::lock_guard<std::mutex> lock(dispatcherMutex());
    return static_cast<bool>(dispatcherStorage());
}

void postToMainThread(std::function<void()> fn)
{
    MainThreadDispatch dispatch;
    {
        std::lock_guard<std::mutex> lock(dispatcherMutex());
        dispatch = dispatcherStorage(); // copy: the call must not hold the lock
    }
    if (dispatch)
    {
        dispatch(std::move(fn));
        return;
    }
    fn(); // no installed dispatcher: the calling thread owns the callback
}

} // namespace mvcore
