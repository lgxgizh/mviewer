#include "core/update/UpdatePrompt.h"

#include "core/update/UpdateChecker.h"

namespace mviewer::core
{

namespace
{

bool skippedCoversLatest(const std::string &latest, const std::string &skipped)
{
    if (skipped.empty())
        return false;
    const auto order = compareReleaseVersions(latest, skipped);
    return order.has_value() && *order <= 0;
}

} // namespace

UpdatePromptDecision decideUpdatePrompt(bool silent, const std::string &latest,
                                        const std::string &skipped, bool uiBusy)
{
    UpdatePromptDecision decision;
    if (!silent)
    {
        decision.kind = UpdatePromptKind::Dialog;
        return decision;
    }
    if (skippedCoversLatest(latest, skipped))
    {
        decision.kind = UpdatePromptKind::None;
        return decision;
    }
    // Both arms are StatusBarNotice: a silent check never opens a modal,
    // whether the viewer, compare, or USM UI is busy.
    if (uiBusy)
    {
        decision.kind = UpdatePromptKind::StatusBarNotice;
        return decision;
    }
    decision.kind = UpdatePromptKind::StatusBarNotice;
    return decision;
}

UpdateCheckStart beginUpdateCheck(UpdateCheckGate &gate, bool silent)
{
    if (!gate.checking)
    {
        gate.checking = true;
        gate.manualPending = false;
        gate.requestWasManual = !silent;
        return UpdateCheckStart::Started;
    }
    if (!silent)
    {
        gate.manualPending = true;
        return UpdateCheckStart::ManualPending;
    }
    return UpdateCheckStart::Ignored;
}

bool finishUpdateCheck(UpdateCheckGate &gate)
{
    const bool asManual = gate.requestWasManual || gate.manualPending;
    gate.checking = false;
    gate.manualPending = false;
    gate.requestWasManual = false;
    return asManual;
}

} // namespace mviewer::core
