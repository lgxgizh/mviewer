#pragma once

// Qt-free update presentation policy. Silent checks never choose a modal.

#include <string>

namespace mviewer::core
{

enum class UpdatePromptKind
{
    None,
    StatusBarNotice,
    Dialog
};

struct UpdatePromptDecision
{
    UpdatePromptKind kind = UpdatePromptKind::None;
};

// Manual checks always use Dialog. A silent check whose latest version is less
// than or equal to the skipped version uses None. Incomparable versions are
// not treated as skipped. Every other silent check uses StatusBarNotice,
// whether or not the UI is busy.
UpdatePromptDecision decideUpdatePrompt(bool silent, const std::string &latest,
                                        const std::string &skipped, bool uiBusy);

enum class UpdateCheckStart
{
    Started,
    ManualPending,
    Ignored
};

struct UpdateCheckGate
{
    bool checking = false;
    bool manualPending = false;
    bool requestWasManual = false;
};

UpdateCheckStart beginUpdateCheck(UpdateCheckGate &gate, bool silent);

// Clears `checking` and reports whether the result should be presented as a
// manual check (the request itself was manual, or a manual check arrived
// while one was already running).
bool finishUpdateCheck(UpdateCheckGate &gate);

} // namespace mviewer::core
