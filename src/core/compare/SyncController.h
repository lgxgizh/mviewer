#pragma once
#include <cstdint>

// Forward declared; CompareEngine is the facade that owns this controller.
class CompareEngine;

// SyncController: manages shared zoom/pan/scroll across all compare cells.
// Emits transformChanged on change (via engine callback).
class SyncController
{
public:
    explicit SyncController(CompareEngine& engine);

    void setScale(double s);
    void setOffset(double ox, double oy);
    void zoomAt(double viewX, double viewY, double factor, int exceptIndex = -1);
    void setEnabled(bool on);
    bool enabled() const;
    double scale() const;
    void offset(double& ox, double& oy) const;

private:
    CompareEngine& m_engine;
};
