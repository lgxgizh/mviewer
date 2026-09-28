#pragma once

namespace mviewer::ui
{

enum class TemporaryAction
{
    None,
    ShowPair,
    ShowDigit,
    KeepLayoutPreset,
    HintMoveMouse,
    HintUseDigits
};

struct TemporaryKeyDecision
{
    TemporaryAction action = TemporaryAction::None;
    int targetPane = -1;
    int sourcePane = -1;
};

// Two panes: the pane under the mouse temporarily shows the other pane.
// Off the panes, classic A<-B still puts B onto pane 0.
inline TemporaryKeyDecision decidePairHold(int imageCount, int hoveredPane)
{
    TemporaryKeyDecision decision;
    if (imageCount > 2)
    {
        decision.action = TemporaryAction::HintUseDigits;
        return decision;
    }
    if (imageCount != 2)
        return decision;
    // Hovered pane is the target; the other pane is the source.
    // No hover: classic A<-B (put B onto pane 0) so toolbar/Space off-pane still work.
    decision.action = TemporaryAction::ShowPair;
    if (hoveredPane == 0 || hoveredPane == 1)
    {
        decision.targetPane = hoveredPane;
        decision.sourcePane = 1 - hoveredPane;
    }
    else
    {
        decision.sourcePane = 1;
        decision.targetPane = 0;
    }
    return decision;
}

// More than two panes: digit K shows image K on the hovered pane.
// Outside a pane, plain 1–8 keeps the layout preset.
inline TemporaryKeyDecision decideDigitHold(int imageCount, int hoveredPane, int digit)
{
    TemporaryKeyDecision decision;
    if (digit < 1 || digit > 8)
        return decision;
    const bool onPane = hoveredPane >= 0 && hoveredPane < imageCount;
    if (imageCount <= 2 || !onPane)
    {
        decision.action = TemporaryAction::KeepLayoutPreset;
        return decision;
    }
    if (digit > imageCount)
    {
        decision.action = TemporaryAction::HintUseDigits;
        return decision;
    }
    decision.action = TemporaryAction::ShowDigit;
    decision.targetPane = hoveredPane;
    decision.sourcePane = digit - 1;
    return decision;
}

} // namespace mviewer::ui
