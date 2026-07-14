#pragma once
class CompareEngine;

// BlinkController: timer-based alternating highlight for blink comparison.
class BlinkController
{
public:
    explicit BlinkController(CompareEngine& engine);

    void setBlinkIndex(int idx);
    void clearBlink();
    int blinkIndex() const;

private:
    CompareEngine& m_engine;
};
