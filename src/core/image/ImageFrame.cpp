#include "core/image/ImageFrame.h"

#include "core/image/ImageBuffer.h"
#include "core/perf/MemoryTracker.h"

#include <cstdint>

#include <QFileInfo>
#include <algorithm>
#include <cstring>
#include <functional>

bool ImageFrame::raw16At(int x, int y, uint16_t &r, uint16_t &g, uint16_t &b) const
{
    if (!m_raw16 || m_raw16->empty())
        return false;
    const int w = m_pixels.width;
    const int h = m_pixels.height;
    if (x < 0 || y < 0 || x >= w || y >= h)
        return false;
    const size_t idx = (static_cast<size_t>(y) * static_cast<size_t>(w) + static_cast<size_t>(x)) *
                       static_cast<size_t>(m_rawCh);
    const auto &buf = *m_raw16;
    if (idx + static_cast<size_t>(m_rawCh) > buf.size())
        return false;
    if (m_rawCh >= 3)
    {
        r = buf[idx];
        g = buf[idx + 1];
        b = buf[idx + 2];
    }
    else
    {
        const auto v = buf[idx];
        r = g = b = v;
    }
    return true;
}

ImageFrame::ImageFrame()
{
    mviewer::perf::MemoryTracker::notifyFrameCreated();
}

ImageFrame::ImageFrame(const mviewer::domain::ImageMetadata &meta, const ImageData &pixels)
    : m_meta(meta), m_pixels(pixels)
{
    m_sequence.valid = true;
    m_sequence.frameCount = std::max(1, meta.frameCount);
    m_sequence.animated = meta.animated;
    m_sequence.kind = meta.sequenceKind == "pages"
                          ? mviewer::core::FrameSequenceKind::Pages
                          : (meta.sequenceKind == "animation"
                                 ? mviewer::core::FrameSequenceKind::Animation
                                 : mviewer::core::FrameSequenceKind::Static);
    m_sequence.loopCount = meta.loopCount;
    m_sequence.totalDurationMs = meta.durationMs;
    m_frameIdentity.fileRevision = meta.hash.empty() ? meta.filePath : meta.hash;
    m_frameIdentity.frameIndex = meta.currentFrame;
    mviewer::perf::MemoryTracker::notifyFrameCreated();
}

ImageFrame::~ImageFrame()
{
    mviewer::perf::MemoryTracker::notifyFrameDestroyed();
}

/*static*/ ImageFrame ImageFrame::create(const std::string &path, const ImageData &pixels)
{
    mviewer::domain::ImageMetadata meta;
    const QFileInfo fi(QString::fromUtf8(path.data(), static_cast<int>(path.size())));
    meta.filePath = path;
    meta.fileName = fi.fileName().toUtf8().toStdString();
    meta.width = pixels.width;
    meta.height = pixels.height;
    meta.fileSize = static_cast<int64_t>(fi.size());
    meta.modifiedEpochSec = fi.lastModified().toSecsSinceEpoch();
    const std::string composite =
        path + "|" + std::to_string(meta.fileSize) + "|" + std::to_string(meta.modifiedEpochSec);
    meta.hash = std::to_string(std::hash<std::string>{}(composite));
    return ImageFrame(meta, pixels);
}

mviewer::domain::ImageId ImageFrame::id() const
{
    // Static images retain their historical file-revision identity. A frame or
    // page inside a container is a distinct render/analysis source, however,
    // so it must never alias another frame in clients such as TileCache.
    if (m_sequence.frameCount <= 1 && m_frameIdentity.frameIndex == 0)
        return mviewer::domain::ImageId{m_meta.hash};
    const std::string &revision =
        m_frameIdentity.fileRevision.empty() ? m_meta.hash : m_frameIdentity.fileRevision;
    return mviewer::domain::ImageId{revision + "#frame=" +
                                    std::to_string(std::max(0, m_frameIdentity.frameIndex))};
}

void ImageFrame::setSequenceIdentity(const mviewer::core::FrameSequenceInfo &sequence,
                                      const mviewer::core::FrameIdentity &identity)
{
    m_sequence = sequence;
    m_frameIdentity = identity;
    m_meta.frameCount = sequence.frameCount;
    m_meta.currentFrame = identity.frameIndex;
    m_meta.durationMs = sequence.totalDurationMs;
    m_meta.loopCount = sequence.loopCount;
    m_meta.animated = sequence.animated;
    m_meta.sequenceKind = sequence.kind == mviewer::core::FrameSequenceKind::Pages
                              ? "pages"
                              : (sequence.kind == mviewer::core::FrameSequenceKind::Animation
                                     ? "animation"
                                     : "static");
}

void ImageFrame::setPixels(const ImageData &pixels)
{
    if (pixels.isNull())
        return;
    m_pixels = pixels;
    m_meta.width = pixels.width;
    m_meta.height = pixels.height;
    // Pixel content changed: any cached histogram is now stale.
    m_histogramComputed = false;
    m_histogram.clear();
}

const mviewer::domain::Histogram &ImageFrame::histogram() const
{
    return m_histogram;
}

void ImageFrame::computeHistogram()
{
    if (m_histogramComputed || m_pixels.isNull())
        return;
    m_histogram.clear();
    const ImageBuffer v = m_pixels.view();
    const int w = v.width, h = v.height, cpp = v.channelsPerPixel();
    const int64_t n = static_cast<int64_t>(w) * h;
    if (n == 0)
        return;
    // Format-aware read. Grayscale8 stores one byte per pixel, so the previous
    // unconditional p[1]/p[2] read ran one to two bytes past the buffer at the
    // final pixel and reported a neighbouring pixel's bytes as green/blue.
    const bool gray = (m_pixels.format == PixelFormat::Grayscale8);
    const bool bgr =
        (m_pixels.format == PixelFormat::BGR24 || m_pixels.format == PixelFormat::BGRA32);
    int64_t sumL = 0, sumR = 0, sumG = 0, sumB = 0;
    if (gray)
    {
        for (int y = 0; y < h; ++y)
        {
            const uint8_t *p = v.data + static_cast<size_t>(y) * v.stride();
            const uint8_t *end = p + w;
            for (; p < end; ++p)
            {
                const uint8_t gVal = *p;
                ++m_histogram.luminance[gVal];
                ++m_histogram.red[gVal];
                ++m_histogram.green[gVal];
                ++m_histogram.blue[gVal];
                sumR += gVal;
                sumG += gVal;
                sumB += gVal;
                sumL += gVal;
            }
        }
    }
    else if (bgr)
    {
        for (int y = 0; y < h; ++y)
        {
            const uint8_t *p = v.data + static_cast<size_t>(y) * v.stride();
            const uint8_t *end = p + static_cast<size_t>(w) * static_cast<size_t>(cpp);
            for (; p < end; p += cpp)
            {
                const uint8_t bVal = p[0];
                const uint8_t gVal = p[1];
                const uint8_t rVal = p[2];
                const int lum = luminance(rVal, gVal, bVal);
                ++m_histogram.luminance[lum];
                ++m_histogram.red[rVal];
                ++m_histogram.green[gVal];
                ++m_histogram.blue[bVal];
                sumR += rVal;
                sumG += gVal;
                sumB += bVal;
                sumL += lum;
            }
        }
    }
    else
    {
        for (int y = 0; y < h; ++y)
        {
            const uint8_t *p = v.data + static_cast<size_t>(y) * v.stride();
            const uint8_t *end = p + static_cast<size_t>(w) * static_cast<size_t>(cpp);
            for (; p < end; p += cpp)
            {
                const uint8_t rVal = p[0];
                const uint8_t gVal = p[1];
                const uint8_t bVal = p[2];
                const int lum = luminance(rVal, gVal, bVal);
                ++m_histogram.luminance[lum];
                ++m_histogram.red[rVal];
                ++m_histogram.green[gVal];
                ++m_histogram.blue[bVal];
                sumR += rVal;
                sumG += gVal;
                sumB += bVal;
                sumL += lum;
            }
        }
    }
    m_histogram.lumMean = static_cast<double>(sumL) / n;
    m_histogram.rMean = static_cast<double>(sumR) / n;
    m_histogram.gMean = static_cast<double>(sumG) / n;
    m_histogram.bMean = static_cast<double>(sumB) / n;
    m_histogramComputed = true;
}

// ─── Tags ───────────────────────────────────────────────────────────────────

void ImageFrame::addTag(const std::string &tag)
{
    if (!hasTag(tag))
        m_tags.push_back(tag);
}

void ImageFrame::removeTag(const std::string &tag)
{
    m_tags.erase(std::remove(m_tags.begin(), m_tags.end(), tag), m_tags.end());
}

bool ImageFrame::hasTag(const std::string &tag) const
{
    return std::find(m_tags.begin(), m_tags.end(), tag) != m_tags.end();
}

// ─── Analysis cache ─────────────────────────────────────────────────────────

const AnalysisCacheEntry *ImageFrame::findAnalysis(const std::string &analyzer) const
{
    for (const auto &e : m_analysisCache)
        if (e.analyzerName == analyzer)
            return &e;
    return nullptr;
}

void ImageFrame::setAnalysisResult(const std::string &analyzer, bool ok)
{
    for (auto &e : m_analysisCache)
    {
        if (e.analyzerName == analyzer)
        {
            e.ok = ok;
            e.populated = true;
            return;
        }
    }
    m_analysisCache.push_back({analyzer, ok, true});
}

void ImageFrame::clearAnalysisCache()
{
    m_analysisCache.clear();
}

// ─── Render cache ───────────────────────────────────────────────────────────

const RenderCacheEntry *ImageFrame::findRenderCache(RenderCacheEntry::Tag tag) const
{
    for (const auto &e : m_renderCache)
        if (e.tag == tag)
            return &e;
    return nullptr;
}

void ImageFrame::setRenderCache(const RenderCacheEntry &entry)
{
    for (auto &e : m_renderCache)
    {
        if (e.tag == entry.tag)
        {
            e = entry;
            return;
        }
    }
    m_renderCache.push_back(entry);
}

void ImageFrame::clearRenderCache()
{
    m_renderCache.clear();
}
