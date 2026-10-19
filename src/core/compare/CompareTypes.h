#pragma once

struct CellPoint
{
    int x = 0, y = 0;
};
struct CellSize
{
    int w = 0, h = 0;
};
struct Vec2
{
    double x = 0.0, y = 0.0;
};

// Comparison grid layout rule
struct CompareLayout
{
    int cols = 0, rows = 0, imageCount = 0;
    static CompareLayout forCount(int n);
    CellPoint cellPos(int index, const CellSize &viewport) const;
    CellSize cellSize(const CellSize &viewport) const;
};

// Auto grid: rows = floor(sqrt(n)), cols = ceil(n / rows). This keeps the
// grid landscape-biased (cols >= rows, matching wide viewports and landscape
// photos) while leaving at most a few empty cells:
//   2 -> 1x2, 3 -> 1x3, 4 -> 2x2, 5/6 -> 2x3, 7/8 -> 2x4, 9 -> 3x3.
inline CompareLayout CompareLayout::forCount(int n)
{
    CompareLayout l;
    l.imageCount = n;
    if (n <= 1)
    {
        l.cols = l.rows = 1;
        return l;
    }
    int rows = 1;
    while ((rows + 1) * (rows + 1) <= n)
        ++rows;
    l.rows = rows;
    l.cols = (n + rows - 1) / rows;
    return l;
}

inline CellPoint CompareLayout::cellPos(int index, const CellSize &viewport) const
{
    if (imageCount <= 0)
        return CellPoint{0, 0};
    const int cellW = viewport.w / cols;
    const int cellH = viewport.h / rows;
    const int c = index % cols;
    const int r = index / cols;
    return CellPoint{c * cellW, r * cellH};
}

inline CellSize CompareLayout::cellSize(const CellSize &viewport) const
{
    if (imageCount <= 0)
        return viewport;
    return CellSize{viewport.w / cols, viewport.h / rows};
}

struct SyncTransform
{
    double scale = 1.0;
    Vec2 offset;
    bool enabled = true;
    bool zoomEnabled = true;
    bool dragEnabled = true;
};

// Per-cell view state. Named CellState inside controllers; CellTransform is
// kept as a backward-compatible alias (same shape: scale + Vec2 offset).
struct CellState
{
    double scale = 1.0;
    Vec2 offset;
};
using CellTransform = CellState;

enum class CompareState
{
    Idle,
    Comparing,
    SyncZoom,
    SyncDrag
};
