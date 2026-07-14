#include "core/compare/BlinkController.h"
#include "core/compare/CompareEngine.h"

BlinkController::BlinkController(CompareEngine& engine) : m_engine(engine) {}

void BlinkController::setBlinkIndex(int idx) { m_engine.setBlinkIndex(idx); }
void BlinkController::clearBlink() { m_engine.clearBlink(); }
int BlinkController::blinkIndex() const { return m_engine.blinkIndex(); }
bool BlinkController::isBlinking() const { return m_engine.blinkIndex() >= 0; }
