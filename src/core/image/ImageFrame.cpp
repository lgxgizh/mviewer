#include "core/image/ImageFrame.h"
#include <algorithm>
#include <cstring>

namespace {

inline int toLum(uint8_t r, uint8_t g, uint8_t b) {
    return static_cast<int>(0.299 * r + 0.587 * g + 0.114 * b);
}

} // namespace

ImageFrame::ImageFrame(const mviewer::domain::ImageMetadata& meta, const ImageData& pixels)
    : m_meta(meta), m_pixels(pixels) {}

/*static*/ ImageFrame ImageFrame::create(const std::string& path, const ImageData& pixels) {
    return ImageFrame(mviewer::domain::ImageMetadata{}, pixels);
}

mviewer::domain::ImageId ImageFrame::id() const {
    return mviewer::domain::ImageId{m_meta.hash};
}

const mviewer::domain::Histogram& ImageFrame::histogram() const {
    return m_histogram;
}

void ImageFrame::computeHistogram() {
    if (m_histogramComputed || m_pixels.isNull()) return;

    m_histogram.clear();

    const ImageBuffer v = m_pixels.view();
    const int w = v.width, h = v.height, cpp = v.channelsPerPixel();
    const int64_t n = static_cast<int64_t>(w) * h;
    if (n == 0) return;

    int64_t sumL = 0, sumR = 0, sumG = 0, sumB = 0;

    for (int y = 0; y < h; ++y) {
        const uint8_t* line = v.data + static_cast<size_t>(y) * v.stride();
        for (int x = 0; x < w; ++x) {
            const uint8_t* p = line + static_cast<size_t>(x) * cpp;
            const int r = p[0], g = p[1], b = p[2];
            ++m_histogram.luminance[std::clamp(toLum(r,g,b), 0, 255)];
            ++m_histogram.red[std::clamp(r, 0, 255)];
            ++m_histogram.green[std::clamp(g, 0, 255)];
            ++m_histogram.blue[std::clamp(b, 0, 255)];
            sumR += r; sumG += g; sumB += b;
            sumL += toLum(r, g, b);
        }
    }

    m_histogram.lumMean = static_cast<double>(sumL) / n;
    m_histogram.rMean = static_cast<double>(sumR) / n;
    m_histogram.gMean = static_cast<double>(sumG) / n;
    m_histogram.bMean = static_cast<double>(sumB) / n;
    m_histogramComputed = true;
}
