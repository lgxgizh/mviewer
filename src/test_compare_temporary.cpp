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
    expect(left.action == TemporaryAction::ShowPair && left.targetPane == 0 && left.sourcePane == 1,
           "hover left shows the right image on the left");
    const auto right = decidePairHold(2, 1);
    expect(right.action == TemporaryAction::ShowPair && right.targetPane == 1 &&
               right.sourcePane == 0,
           "hover right shows the left image on the right");
    const auto noHover = decidePairHold(2, -1);
    expect(noHover.action == TemporaryAction::ShowPair && noHover.targetPane == 0 &&
               noHover.sourcePane == 1,
           "no hover keeps classic A<-B");
    const auto offPane = decidePairHold(2, 2);
    expect(offPane.action == TemporaryAction::ShowPair && offPane.targetPane == 0 &&
               offPane.sourcePane == 1,
           "cursor off the panes keeps classic A<-B");
    expect(decidePairHold(3, 0).action == TemporaryAction::HintUseDigits, "space unused above 2");
    expect(decidePairHold(1, 0).action == TemporaryAction::None, "single image has no hold");

    const auto digit = decideDigitHold(4, 2, 1);
    expect(digit.action == TemporaryAction::ShowDigit && digit.targetPane == 2 &&
               digit.sourcePane == 0,
           "digit 1 shows image 1 on the hovered pane");
    const auto digitOffPane = decideDigitHold(4, -1, 2);
    expect(digitOffPane.action == TemporaryAction::ShowDigit && digitOffPane.targetPane == 0 &&
               digitOffPane.sourcePane == 1,
           "digit off-pane previews on pane 0 instead of a layout preset");
    const auto focused = decideDigitHold(3, -1, 1, 2);
    expect(focused.action == TemporaryAction::ShowDigit && focused.targetPane == 2 &&
               focused.sourcePane == 0,
           "digit 1 off-pane previews image 1 on the fallback pane");
    const auto two = decideDigitHold(2, 0, 1);
    expect(two.action == TemporaryAction::ShowDigit && two.targetPane == 0 && two.sourcePane == 0,
           "two images preview on digits instead of a layout preset");
    const auto twoOff = decideDigitHold(2, -1, 2, 1);
    expect(twoOff.action == TemporaryAction::ShowDigit && twoOff.targetPane == 1 &&
               twoOff.sourcePane == 1,
           "two images off-pane preview image 2 on the fallback pane");
    expect(decideDigitHold(3, 1, 8).action == TemporaryAction::HintUseDigits,
           "digit past count only hints");
    expect(decideDigitHold(3, -1, 4).action == TemporaryAction::HintUseDigits,
           "off-pane digit past count only hints");
    expect(decideDigitHold(0, 0, 1).action == TemporaryAction::HintUseDigits,
           "no images only hint");
    const auto badFallback = decideDigitHold(4, 9, 3, 1);
    expect(badFallback.action == TemporaryAction::ShowDigit && badFallback.targetPane == 1 &&
               badFallback.sourcePane == 2,
           "a hover outside the image range uses the fallback pane");

    if (g_failed)
        return 1;
    std::printf("compare temporary decisions ok\n");
    return 0;
}
