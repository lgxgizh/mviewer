#pragma once

namespace mviewer::ui
{

enum class TemporaryAction
{
    None,
    ShowPair,
    ShowDigit,
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

// Digit K (1..8) temporarily shows image K. The pane under the pointer is the
// target. Off a pane, fallbackPane is used when it names a real image; otherwise
// pane 0. A digit past the image count only asks for a hint. Plain digits never
// select a layout preset, including when only one or two images are open.
inline TemporaryKeyDecision decideDigitHold(int imageCount, int hoveredPane, int digit,
                                            int fallbackPane = -1)
{
    TemporaryKeyDecision decision;
    if (digit < 1 || digit > 8)
        return decision;
    if (imageCount < 1 || digit > imageCount)
    {
        decision.action = TemporaryAction::HintUseDigits;
        return decision;
    }
    const bool onPane = hoveredPane >= 0 && hoveredPane < imageCount;
    int target = onPane ? hoveredPane : fallbackPane;
    if (target < 0 || target >= imageCount)
        target = 0;
    decision.action = TemporaryAction::ShowDigit;
    decision.targetPane = target;
    decision.sourcePane = digit - 1;
    return decision;
}

} // namespace mviewer::ui
