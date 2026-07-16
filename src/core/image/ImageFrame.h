#pragma once
#include "domain/Histogram.h"
#include "domain/Image.h"
#include "domain/Selection.h"

#include "ImageBuffer.h"

#include <optional>
#include <string>
#include <vector>

enum class DecodeState : uint8_t
{
    Idle,
    Decoding,
    Decoded,
    Failed
};
enum class CacheState : uint8_t
{
    None,
    Memory,
    Disk
};

// AnalysisCache: holds pre-computed analysis results (e.g., RGBMean, entropy).
// One entry per analyzer name; allows multiple analyzers to cache results.
struct AnalysisCacheEntry
{
    std::string analyzerName;
    bool ok = false;
    bool populated = false;
};

// RenderCache: holds scaled/processed sub-images used by the UI.
struct RenderCacheEntry
{
    enum class Tag : uint8_t
    {
        ScaledView,
        ThumbnailOverlay,
        DiffOverlay
    };
    Tag tag;
    ImageData data;
    int srcWidth = 0;
    int srcHeight = 0;
};

// Universal domain object: holds pixels, metadata, cache, state, and ownership.
// All engines pass ImageFrame; no external ad-hoc data structures.
// Thread-safe for read-only access; cache/decode state changes are atomic.
class ImageFrame
{
  public:
    ImageFrame() = default;
    ImageFrame(const mviewer::domain::ImageMetadata &meta, const ImageData &pixels);

    // Factory: create from a decoded image, filling metadata (hash, size, mtime).
    // Implementation may use Qt for file interrogation (.cpp only).
    static ImageFrame create(const std::string &path, const ImageData &pixels);

    // ─── Identity ─────────────────────────────────────────────────────────────
    const mviewer::domain::ImageMetadata &metadata() const
    {
        return m_meta;
    }
    mviewer::domain::ImageId id() const;

    // ─── Pixel access ────────────────────────────────────────────────────────
    const ImageData &pixels() const
    {
        return m_pixels;
    }
    const ImageBuffer view() const
    {
        return m_pixels.view();
    }
    // Replace the pixel buffer (e.g. after a reversible Rotate command). Updates
    // metadata dimensions to match. Callers must ensure the new buffer is valid.
    void setPixels(const ImageData &pixels);
    int width() const
    {
        return m_pixels.width;
    }
    int height() const
    {
        return m_pixels.height;
    }
    bool isValid() const
    {
        return !m_pixels.isNull();
    }

    // ─── Thumbnail (lazy) ────────────────────────────────────────────────────
    const ImageData &thumbnail() const
    {
        return m_thumbnail;
    }
    void setThumbnail(const ImageData &t)
    {
        m_thumbnail = t;
    }
    bool hasThumbnail() const
    {
        return !m_thumbnail.isNull();
    }

    // ─── Decode / Cache states ──────────────────────────────────────────────
    DecodeState decodeState() const
    {
        return m_decodeState;
    }
    void setDecodeState(DecodeState s)
    {
        m_decodeState = s;
    }
    CacheState cacheState() const
    {
        return m_cacheState;
    }
    void setCacheState(CacheState s)
    {
        m_cacheState = s;
    }

    // ─── Histogram (lazy-computed) ───────────────────────────────────────────
    const mviewer::domain::Histogram &histogram() const;
    void computeHistogram();
    bool hasHistogram() const
    {
        return m_histogramComputed;
    }

    // ─── Metadata ────────────────────────────────────────────────────────────
    void setMetadata(const mviewer::domain::ImageMetadata &m)
    {
        m_meta = m;
    }

    // ─── Selection ───────────────────────────────────────────────────────────
    const mviewer::domain::Selection &selection() const
    {
        return m_selection;
    }
    void setSelection(const mviewer::domain::Selection &s)
    {
        m_selection = s;
    }
    void clearSelection()
    {
        m_selection = {};
    }

    // ─── Tags ───────────────────────────────────────────────────────────────
    const std::vector<std::string> &tags() const
    {
        return m_tags;
    }
    void addTag(const std::string &tag);
    void removeTag(const std::string &tag);
    bool hasTag(const std::string &tag) const;

    // ─── Analysis cache ─────────────────────────────────────────────────────
    const AnalysisCacheEntry *findAnalysis(const std::string &analyzer) const;
    const std::vector<AnalysisCacheEntry> &analysisCache() const
    {
        return m_analysisCache;
    }
    void setAnalysisResult(const std::string &analyzer, bool ok);
    void clearAnalysisCache();

    // ─── Render cache ───────────────────────────────────────────────────────
    const RenderCacheEntry *findRenderCache(RenderCacheEntry::Tag tag) const;
    void setRenderCache(const RenderCacheEntry &entry);
    void clearRenderCache();

    // ─── Stats convenience ──────────────────────────────────────────────────
    double luminanceMean() const
    {
        return m_histogram.lumMean;
    }
    void rgbMeans(double &r, double &g, double &b) const
    {
        r = m_histogram.rMean;
        g = m_histogram.gMean;
        b = m_histogram.bMean;
    }

  private:
    mviewer::domain::ImageMetadata m_meta;
    ImageData m_pixels;
    DecodeState m_decodeState = DecodeState::Idle;
    CacheState m_cacheState = CacheState::None;
    bool m_histogramComputed = false;
    mviewer::domain::Histogram m_histogram;
    ImageData m_thumbnail;

    mviewer::domain::Selection m_selection;
    std::vector<std::string> m_tags;
    std::vector<AnalysisCacheEntry> m_analysisCache;
    std::vector<RenderCacheEntry> m_renderCache;
};
