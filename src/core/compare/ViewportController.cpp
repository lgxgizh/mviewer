#include "core/compare/ViewportController.h"

#include "core/compare/CompareEngine.h"

ViewportController::ViewportController(CompareEngine& engine)
    : m_engine(engine)
{
}

int ViewportController::cols() const
{
    return m_engine.layout().cols;
}
int ViewportController::rows() const
{
    return m_engine.layout().rows;
}
int ViewportController::imageCount() const
{
    return m_engine.imageCount();
}
CellPoint ViewportController::cellPos(int index, const CellSize& viewport) const
{
    return m_engine.layout().cellPos(index, viewport);
}
CellSize ViewportController::cellSize(const CellSize& viewport) const
{
    return m_engine.layout().cellSize(viewport);
}
