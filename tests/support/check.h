#pragma once

// Shared test helpers (review P2-7 / P2-8).
//
//   MVIEWER_CHECK(cond, msg)  counts failures in mviewer::test::failures()
//   mviewer::test::waitFor(pred, timeoutMs)
//       Event-driven wait: pumps the Qt event loop and returns as soon as the
//       predicate holds. The timeout is a cap, not a sleep, and is multiplied
//       by MVIEWER_TEST_TIMEOUT_SCALE (default 1) so slow CI runners can be
//       given more headroom without editing tests.
//   mviewer::test::pump(ms)   process events for about `ms` (scaled)
//
// Header-only and Qt-Core-only so any test target can include it via
// target_include_directories(<test> PRIVATE ${CMAKE_SOURCE_DIR}/tests/support).

#include <QCoreApplication>
#include <QDeadlineTimer>
#include <QEventLoop>
#include <QTimer>
#include <QtGlobal>

#include <iostream>

namespace mviewer::test
{

inline int &failures()
{
    static int count = 0;
    return count;
}

inline double timeoutScale()
{
    bool ok = false;
    const double scale = qEnvironmentVariable("MVIEWER_TEST_TIMEOUT_SCALE").toDouble(&ok);
    return ok && scale > 0.0 ? scale : 1.0;
}

inline int scaledTimeoutMs(int timeoutMs)
{
    return static_cast<int>(timeoutMs * timeoutScale());
}

template <typename Predicate> bool waitFor(Predicate &&predicate, int timeoutMs = 5000)
{
    QDeadlineTimer deadline(scaledTimeoutMs(timeoutMs));
    // A 10 ms heartbeat bounds each blocking wait so worker results that
    // arrive without a posted event are still observed promptly.
    QTimer heartbeat;
    heartbeat.start(10);
    while (!predicate())
    {
        if (deadline.hasExpired())
            return predicate();
        // Blocks until the next event (queued signal, timer, worker result)
        // instead of sleeping a fixed interval.
        QCoreApplication::processEvents(QEventLoop::AllEvents | QEventLoop::WaitForMoreEvents);
    }
    return true;
}

inline void pump(int ms)
{
    QDeadlineTimer deadline(scaledTimeoutMs(ms));
    while (!deadline.hasExpired())
        QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
}

} // namespace mviewer::test

#define MVIEWER_CHECK(cond, msg)                                                                   \
    do                                                                                             \
    {                                                                                              \
        if (cond)                                                                                  \
        {                                                                                          \
            std::cout << "  PASS: " << (msg) << std::endl;                                         \
        }                                                                                          \
        else                                                                                       \
        {                                                                                          \
            std::cout << "  FAIL: " << (msg) << " (" << __FILE__ << ":" << __LINE__ << ")"         \
                      << std::endl;                                                                \
            ++mviewer::test::failures();                                                           \
        }                                                                                          \
    } while (0)
