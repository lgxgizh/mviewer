#pragma once
#include "ImageBuffer.h"
#include "domain/Image.h"
#include "domain/Histogram.h"
#include <string>

enum class DecodeState : uint8_t { Idle, Decoding, Decoded, Failed };
enum class CacheState  : uint8_t { None, Memory, Disk };

// Runtime image holder — the universal image object for all engines.
// Wraps ImageData + decode state + histogram + cache state.
// Header is Qt-free; engines operate on this type exclusively.
class ImageFrame
{
public:
    ImageFrame() = default;
    ImageFrame(const mviewer::domain::ImageMetadata& meta, const ImageData& pixels);

    // Build a frame from a decoded image, filling metadata (hash, size, mtime).
    // Implementation may use Qt for file interrogation; header stays Qt-free.
    static ImageFrame create(const std::string& path, const ImageData& pixels);

    // Identity
    const mviewer::domain::ImageMetadata& metadata() const { return m_meta; }
    mviewer::domain::ImageId id() const;

    // Pixel access
    const ImageData& pixels() const { return m_pixels; }
    const ImageBuffer view() const { return m_pixels.view(); }
    int width() const { return m_pixels.width; }
    int height() const { return m_pixels.height; }
    bool isValid() const { return !m_pixels.isNull(); }

    // Decode state
    DecodeState decodeState() const { return m_decodeState; }
    void setDecodeState(DecodeState s) { m_decodeState = s; }

    // Histogram (lazy-computed)
    const mviewer::domain::Histogram& histogram() const;
    void computeHistogram();
    bool hasHistogram() const { return m_histogramComputed; }

    // Cache state
    CacheState cacheState() const { return m_cacheState; }
    void setCacheState(CacheState s) { m_cacheState = s; }

    // Stats convenience
    double luminanceMean() const { return m_histogram.lumMean; }
    void rgbMeans(double& r, double& g, double& b) const {
        r = m_histogram.rMean; g = m_histogram.gMean; b = m_histogram.bMean;
    }

private:
    mviewer::domain::ImageMetadata m_meta;
    ImageData m_pixels;
    DecodeState m_decodeState = DecodeState::Idle;
    CacheState m_cacheState = CacheState::None;
    bool m_histogramComputed = false;
    mviewer::domain::Histogram m_histogram;
};
