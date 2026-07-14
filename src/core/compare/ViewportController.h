#pragma once
struct CellSize;
struct CellPoint;
class CompareEngine;

// ViewportController: per-cell layout (cols, rows) and cell size calculation.
class ViewportController
{
public:
    explicit ViewportController(CompareEngine& engine);

    int cols() const;
    int rows() const;
    int imageCount() const;
    CellPoint cellPos(int index, const CellSize& viewport) const;
    CellSize cellSize(const CellSize& viewport) const;

private:
    CompareEngine& m_engine;
};
