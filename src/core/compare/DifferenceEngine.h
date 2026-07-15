#pragma once

#include "core/image/ImageBuffer.h"

class DifferenceEngine
{
public:
    // Pixel diff of a vs b. Returns Grayscale8 image: identical = black,
    // larger difference = brighter. Supports RGB24/BGR24/RGBA32/BGRA32
    // and Grayscale8 inputs.
    static ImageData differenceMap(const ImageData& a, const ImageData& b);

    // Pseudo-colormap: converts a Grayscale8 image to RGB32 heatmap
    // (blue = cold, green = mid, red = hot).
    static ImageData heatMap(const ImageData& gray);

private:
    // Returns byte offset (0=R, 1=G, 2=B) within a pixel for the given
    // pixel format.
    static int channelOffset(PixelFormat fmt, int channel);
};
