#pragma once

class CompareEngine;

// BlinkController: manages blink comparison state (which image is highlighted).
// Delegates to CompareEngine for state storage but owns the blink logic.
class BlinkController
{
public:
    explicit BlinkController(CompareEngine& engine);

    void setBlinkIndex(int idx);
    void clearBlink();
    int blinkIndex() const;
    bool isBlinking() const;

private:
    CompareEngine& m_engine;
};
