#pragma once

#include "core/image/ImageBuffer.h"
#include "domain/Selection.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

// ─── PixelController ─────────────────────────────────────────────────────────
// Fifth Compare Engine module (Architect P1-② list: Layout / Sync / ROI / Diff
// / Pixel). Owns the synchronized pixel probe: given a cursor position in shared
// image space and the set of compared frames, it reads the pixel from every cell
// and computes per-cell RGB plus the delta against a base cell. Domain-free
// (core/compare, no Qt) so it is unit-testable and reusable by the UI/plugin.
//
// When sync is enabled the probe position is shared across all cells (the same
// image-space point is sampled in every frame). When disabled, each cell keeps
// its own probe (not yet exercised; the engine tracks per-cell transforms).

struct PixelSample
{
    int cellIndex = 0;
    int x = 0;
    int y = 0;
    int r = 0, g = 0, b = 0;
    bool valid = false;
};

struct PixelDelta
{
    int dr = 0, dg = 0, db = 0;
    double dist = 0.0; // euclidean distance in RGB space
};

struct PixelController
{
    // Read the pixel at (imgX,imgY) from each frame's full-res pixels.
    // Returns one PixelSample per frame (valid=false if out of bounds).
    std::vector<PixelSample> probe(const std::vector<ImageData> &frames, int imgX, int imgY) const
    {
        std::vector<PixelSample> out;
        out.reserve(frames.size());
        for (size_t i = 0; i < frames.size(); ++i)
        {
            PixelSample s;
            s.cellIndex = static_cast<int>(i);
            s.x = imgX;
            s.y = imgY;
            const ImageData &f = frames[i];
            if (!f.isNull() && imgX >= 0 && imgY >= 0 && imgX < f.width && imgY < f.height)
            {
                const ImageBuffer v = f.view();
                const uint8_t *p = v.data +
                                   static_cast<size_t>(imgY) * static_cast<size_t>(v.stride()) +
                                   static_cast<size_t>(imgX) * v.channelsPerPixel();
                s.r = p[0];
                s.g = p[1];
                s.b = p[2];
                s.valid = true;
            }
            out.push_back(s);
        }
        return out;
    }

    // Delta of every cell against a base cell (default index 0). Only defined
    // for cells whose own sample is valid; base validity is required.
    std::vector<PixelDelta> deltaAgainst(const std::vector<PixelSample> &samples,
                                         int baseIndex = 0) const
    {
        std::vector<PixelDelta> out;
        out.reserve(samples.size());
        const bool baseOk = baseIndex >= 0 && baseIndex < static_cast<int>(samples.size()) &&
                            samples[baseIndex].valid;
        const PixelSample base = baseOk ? samples[baseIndex] : PixelSample{};
        for (const auto &s : samples)
        {
            PixelDelta d;
            if (baseOk && s.valid)
            {
                d.dr = s.r - base.r;
                d.dg = s.g - base.g;
                d.db = s.b - base.b;
                const double dr = d.dr, dg = d.dg, db = d.db;
                d.dist = std::sqrt(dr * dr + dg * dg + db * db);
            }
            out.push_back(d);
        }
        return out;
    }

    // Convenience: given frames + probe point, return samples + deltas in one call.
    struct ProbeResult
    {
        std::vector<PixelSample> samples;
        std::vector<PixelDelta> deltas;
    };
    ProbeResult inspect(const std::vector<ImageData> &frames, int imgX, int imgY,
                        int baseIndex = 0) const
    {
        ProbeResult r;
        r.samples = probe(frames, imgX, imgY);
        r.deltas = deltaAgainst(r.samples, baseIndex);
        return r;
    }
};
