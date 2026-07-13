#include "core/compare/SyncController.h"
#include "core/compare/CompareEngine.h"

SyncController::SyncController(CompareEngine& engine) : m_engine(engine) {}

void SyncController::setScale(double s) { m_engine.setScale(s); }
void SyncController::setOffset(double ox, double oy) { m_engine.setOffset(ox, oy); }
void SyncController::zoomAt(double viewX, double viewY, double factor, int exceptIndex) {
    m_engine.zoomAt(viewX, viewY, factor, exceptIndex);
}
void SyncController::setEnabled(bool on) { m_engine.setSyncEnabled(on); }
bool SyncController::enabled() const { return m_engine.syncEnabled(); }
double SyncController::scale() const { return m_engine.syncTransform().scale; }
void SyncController::offset(double& ox, double& oy) const {
    ox = m_engine.syncTransform().offset.x;
    oy = m_engine.syncTransform().offset.y;
}
