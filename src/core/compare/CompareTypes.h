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

inline CompareLayout CompareLayout::forCount(int n)
{
    CompareLayout l;
    l.imageCount = n;
    if (n <= 1)
    {
        l.cols = l.rows = 1;
        return l;
    }
    switch (n)
    {
    case 2:
        l.cols = 2;
        l.rows = 1;
        break;
    case 3:
        l.cols = 3;
        l.rows = 1;
        break;
    case 4:
        l.cols = 2;
        l.rows = 2;
        break;
    default:
        l.cols = 4;
        l.rows = 2;
        break;
    }
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
