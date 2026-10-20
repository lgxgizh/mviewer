#pragma once

#include <functional>

namespace mviewer::application
{

// Registers extra stop work for process shutdown (UI services the application
// layer must not include, e.g. ThumbnailCache). Hooks run once, while
// QCoreApplication is still alive, before TaskScheduler::shutdown().
void registerProcessShutdownHook(std::function<void()> hook);

// Stops process-wide workers and Qt-plugin-backed readers while
// QCoreApplication is still alive. Widgets the application owns must already
// have been destroyed. Idempotent. Production main.cpp and tests share this
// path (see docs/adr/017-process-lifetime-services.md).
void shutdownProcessServices();

// Calls shutdownProcessServices() from the destructor. Construct after
// QCoreApplication / QApplication and before any MainWindow so destruction
// order is: widgets, then this guard (join workers), then QApplication.
class ScopedProcessShutdown
{
  public:
    ScopedProcessShutdown();
    ~ScopedProcessShutdown();
    ScopedProcessShutdown(const ScopedProcessShutdown &) = delete;
    ScopedProcessShutdown &operator=(const ScopedProcessShutdown &) = delete;
};

} // namespace mviewer::application
