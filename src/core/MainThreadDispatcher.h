#pragma once

// Explicit main-thread delivery for core services (ADR 016/Qt boundary).
//
// Core services that produce results off the UI thread (MetadataIndexer,
// MetadataPresentationService) must hand them back to the thread that owns the
// process's event loop. They used to decide that themselves by checking
// `QCoreApplication::instance()`, which made the delivery thread depend on
// ambient Qt state: the same call behaved differently in the app, in a headless
// tool without a QCoreApplication, and in a unit test — and no caller could
// control or test the difference.
//
// The decision is now explicit: the process installs a dispatcher once (see
// `main.cpp`, which installs the Qt event-loop dispatcher), and core only asks
// for "the main thread" through this header. Nothing here inspects Qt, and this
// header stays Qt-free (enforced by R5 in scripts/architecture_gate.ps1).
//
// Contract for producers:
//   * deliveries are DEFERRED through the dispatcher, never immediate, so a
//     request cancelled after its worker finished still drops every pending
//     callback (core re-checks its cancellation token inside the closure);
//   * deliveries keep their submission order;
//   * with no dispatcher installed (unit tests, headless tools) the closure
//     runs inline on the calling — worker — thread. Consumers that touch
//     widgets must therefore marshal themselves as well; the UI consumers do
//     (see the QMetaObject::invokeMethod(qApp, ...) hops in
//     thumbnailpanel_filters.cpp, metadatapanel.cpp, metadataoverlay.cpp and
//     mainwindow.cpp).

#include <functional>

namespace mvcore
{

// Runs the given closure on the main thread. Implementations must defer
// (queue it), never run it inline: the caller relies on being able to cancel a
// posted delivery before it executes.
using MainThreadDispatch = std::function<void(std::function<void()>)>;

// Installs the process dispatcher. Call once from the main thread before
// starting core work. Passing an empty function clears it and restores the
// inline fallback.
void setMainThreadDispatcher(MainThreadDispatch dispatch);

bool hasMainThreadDispatcher();

// Delivers `fn` on the main thread: through the installed dispatcher, or
// inline on the calling thread when none is installed.
void postToMainThread(std::function<void()> fn);

} // namespace mvcore
