#pragma once
#include "core/image/ImageBuffer.h"
#include <cstdint>

// DifferenceEngine: pixel diff + heatmap computation.
class DifferenceEngine {
public:
    // Grayscale diff map: |a - b| per channel averaged. Same size as inputs.
    static ImageData differenceMap(const ImageData& a, const ImageData& b);

    // Pseudo-color heatmap: grayscale → blue-green-red gradient.
    static ImageData heatMap(const ImageData& gray);
};
