#include "core/compare/BlinkController.h"

#include <algorithm>

BlinkController::BlinkController(int imageCount)
    : m_imageCount(imageCount)
{
}

void BlinkController::setBlinkIndex(int idx)
{
    if (idx == -1) {
        m_blinkIndex = -1;
        return;
    }
    if (idx >= 0 && idx < m_imageCount)
        m_blinkIndex = idx;
}

void BlinkController::clearBlink()
{
    m_blinkIndex = -1;
}
