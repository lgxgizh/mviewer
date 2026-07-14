#include "core/compare/SelectionController.h"
#include "core/compare/CompareEngine.h"

SelectionController::SelectionController(CompareEngine &engine)
    : m_engine(engine) {}

double SelectionController::cellScale(int index) const {
  return m_engine.cellScale(index);
}
void SelectionController::cellOffset(int index, double &ox, double &oy) const {
  auto v = m_engine.cellOffset(index);
  ox = v.x;
  oy = v.y;
}
void SelectionController::setCellScale(int index, double s) {
  m_engine.setCellScale(index, s);
}
void SelectionController::setCellOffset(int index, double ox, double oy) {
  m_engine.setCellOffset(index, ox, oy);
}
void SelectionController::fitCell(int index, const CellSize &viewport,
                                  const CellSize &imageSize) {
  m_engine.fitCell(index, viewport, imageSize);
}
const CellTransform &SelectionController::cellTransform(int index) const {
  return m_engine.cellTransform(index);
}
