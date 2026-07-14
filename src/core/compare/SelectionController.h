#pragma once
struct CellSize;
struct CellTransform;
class CompareEngine;

// SelectionController: manages per-cell transform (scale, offset) and ROI box
// sync.
class SelectionController
{
public:
    explicit SelectionController(CompareEngine& engine);

    double cellScale(int index) const;
    void cellOffset(int index, double& ox, double& oy) const;
    void setCellScale(int index, double s);
    void setCellOffset(int index, double ox, double oy);
    void fitCell(int index, const CellSize& viewport, const CellSize& imageSize);
    const CellTransform& cellTransform(int index) const;

private:
    CompareEngine& m_engine;
};
