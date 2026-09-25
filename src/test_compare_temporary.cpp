#include "compareworkspace_temporary.h"

#include <cstdio>

namespace
{
using mviewer::ui::decideDigitHold;
using mviewer::ui::decidePairHold;
using mviewer::ui::TemporaryAction;

int g_failed = 0;

void expect(bool cond, const char *message)
{
    if (!cond)
    {
        std::fprintf(stderr, "FAIL %s\n", message);
        ++g_failed;
    }
}
} // namespace

int main()
{
    const auto left = decidePairHold(2, 0);
    expect(left.action == TemporaryAction::ShowPair && left.targetPane == 1 && left.sourcePane == 0,
           "hover left shows left image on the right");
    const auto right = decidePairHold(2, 1);
    expect(right.action == TemporaryAction::ShowPair && right.targetPane == 0 &&
               right.sourcePane == 1,
           "hover right shows right image on the left");
    expect(decidePairHold(2, -1).action == TemporaryAction::HintMoveMouse, "pair needs a pane");
    expect(decidePairHold(3, 0).action == TemporaryAction::HintUseDigits, "space unused above 2");
    expect(decidePairHold(1, 0).action == TemporaryAction::None, "single image has no hold");

    const auto digit = decideDigitHold(4, 2, 1);
    expect(digit.action == TemporaryAction::ShowDigit && digit.targetPane == 2 &&
               digit.sourcePane == 0,
           "digit shows that image on the hovered pane");
    expect(decideDigitHold(4, -1, 2).action == TemporaryAction::KeepLayoutPreset,
           "digit off-pane keeps layout");
    expect(decideDigitHold(2, 0, 1).action == TemporaryAction::KeepLayoutPreset,
           "two panes keep layout presets on digits");
    expect(decideDigitHold(3, 1, 8).action == TemporaryAction::HintUseDigits,
           "digit past count does not change layout");

    if (g_failed)
        return 1;
    std::printf("compare temporary decisions ok\n");
    return 0;
}
