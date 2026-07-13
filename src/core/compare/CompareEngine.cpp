#include "core/compare/CompareEngine.h"
#include "core/image/Decoder.h"
#include "core/image/ImageObject.h"
#include "core/image/ImageFrame.h"
#include "core/image/QtConvert.h"
#include "core/scheduler/TaskScheduler.h"

#include <QImage>
#include <algorithm>
#include <cmath>
#include <cstring>

// Internal pixel-level diff via QtConvert; header stays Qt-free.

CompareLayout CompareLayout::forCount(int n) {
    CompareLayout l;
    l.imageCount = n;
    if (n <= 1) { l.cols = l.rows = 1; return l; }
    switch (n) {
    case 2: l.cols = 2; l.rows = 1; break;
    case 3: l.cols = 3; l.rows = 1; break;
    case 4: l.cols = 2; l.rows = 2; break;
    default:
        l.cols = 4; l.rows = 2; break;
    }
    return l;
}

CellPoint CompareLayout::cellPos(int index, const CellSize& viewport) const {
    if (imageCount <= 0) return CellPoint{0, 0};
    const int cellW = viewport.w / cols;
    const int cellH = viewport.h / rows;
    const int c = index % cols;
    const int r = index / cols;
    return CellPoint{c * cellW, r * cellH};
}

CellSize CompareLayout::cellSize(const CellSize& viewport) const {
    if (imageCount <= 0) return viewport;
    return CellSize{viewport.w / cols, viewport.h / rows};
}

CompareEngine::CompareEngine() {
    m_layout = CompareLayout::forCount(0);
}

void CompareEngine::setImages(const std::vector<std::string>& paths) {
    m_images.clear();
    m_images.reserve(paths.size());
    for (const std::string& p : paths) {
        std::string key = p;
        ImageData img;
        if (DiskCache::instance().get(key, img)) {
            m_images.push_back(ImageFrame::create(p, img));
        } else {
            img = Decoder::decodeFull(p);
            if (!img.isNull()) {
                DiskCache::instance().put(key, img);
                m_images.push_back(ImageFrame::create(p, img));
            }
        }
    }
    rebuildLayout();
    m_blinkIndex = -1;
}

void CompareEngine::addImage(const std::string& path) {
    std::string key = path;
    ImageData img;
    if (DiskCache::instance().get(key, img)) {
        m_images.push_back(ImageFrame::create(path, img));
    } else {
        img = Decoder::decodeFull(path);
        if (!img.isNull()) {
            DiskCache::instance().put(key, img);
            m_images.push_back(ImageFrame::create(path, img));
        }
    }
    if (!m_images.empty())
        rebuildLayout();
}

void CompareEngine::removeImage(int index) {
    if (index < 0 || index >= static_cast<int>(m_images.size())) return;
    m_images.erase(m_images.begin() + index);
    rebuildLayout();
}

void CompareEngine::clear() {
    m_images.clear();
    rebuildLayout();
    m_blinkIndex = -1;
}

const ImageFrame* CompareEngine::imageAt(int index) const {
    if (index < 0 || index >= static_cast<int>(m_images.size())) return nullptr;
    return &m_images[index];
}

void CompareEngine::rebuildLayout() {
    m_layout = CompareLayout::forCount(imageCount());
    m_cells.resize(imageCount());
}

mviewer::domain::CompareSession CompareEngine::session() const {
    mviewer::domain::CompareSession s;
    s.imageIds.reserve(imageCount());
    for (int i = 0; i < imageCount(); ++i)
        s.imageIds.push_back(m_images[i].metadata().filePath);
    s.cells.resize(m_cells.size());
    for (size_t i = 0; i < m_cells.size(); ++i) {
        s.cells[i].scale   = m_cells[i].scale;
        s.cells[i].offsetX = m_cells[i].offset.x;
        s.cells[i].offsetY = m_cells[i].offset.y;
    }
    s.syncMode      = m_sync.enabled ? mviewer::domain::SyncMode::All
                                     : mviewer::domain::SyncMode::Off;
    s.blinkIndex    = m_blinkIndex;
    s.sharedScale   = m_sync.scale;
    s.sharedOffsetX = m_sync.offset.x;
    s.sharedOffsetY = m_sync.offset.y;
    s.cols          = m_layout.cols;
    s.rows          = m_layout.rows;
    return s;
}

void CompareEngine::setScale(double s) {
    m_sync.scale = s;
    if (m_sync.enabled) {
        for (auto& c : m_cells) c.scale = s;
    }
}

void CompareEngine::setOffset(double ox, double oy) {
    m_sync.offset.x = ox; m_sync.offset.y = oy;
    if (m_sync.enabled) {
        for (auto& c : m_cells) { c.offset.x = ox; c.offset.y = oy; }
    }
}

void CompareEngine::zoomAt(double viewX, double viewY, double factor, int exceptIndex) {
    if (m_sync.enabled) {
        const double newScale = m_sync.scale * factor;
        for (int i = 0; i < static_cast<int>(m_cells.size()); ++i) {
            if (i == exceptIndex) continue;
            m_cells[i].scale = newScale;
        }
        m_sync.scale = newScale;
    } else if (exceptIndex >= 0 && exceptIndex < static_cast<int>(m_cells.size())) {
        m_cells[exceptIndex].scale *= factor;
    }
}

void CompareEngine::setCellScale(int index, double s) {
    if (index < 0 || index >= static_cast<int>(m_cells.size())) return;
    m_cells[index].scale = s;
}

void CompareEngine::setCellOffset(int index, double ox, double oy) {
    if (index < 0 || index >= static_cast<int>(m_cells.size())) return;
    m_cells[index].offset.x = ox;
    m_cells[index].offset.y = oy;
}

double CompareEngine::cellScale(int index) const {
    if (index < 0 || index >= static_cast<int>(m_cells.size())) return 1.0;
    return m_cells[index].scale;
}

Vec2 CompareEngine::cellOffset(int index) const {
    if (index < 0 || index >= static_cast<int>(m_cells.size())) return {0.0, 0.0};
    return m_cells[index].offset;
}

void CompareEngine::fitCell(int index, const CellSize& viewport, const CellSize& imageSize) {
    if (index < 0 || index >= static_cast<int>(m_cells.size())) return;
    if (imageSize.w <= 0 || imageSize.h <= 0) return;
    const double scaleX = static_cast<double>(viewport.w) / imageSize.w;
    const double scaleY = static_cast<double>(viewport.h) / imageSize.h;
    m_cells[index].scale = std::min(scaleX, scaleY);
    m_cells[index].offset.x = 0;
    m_cells[index].offset.y = 0;
}

const CellTransform& CompareEngine::cellTransform(int index) const {
    static const CellTransform kDefault{};
    if (index < 0 || index >= static_cast<int>(m_cells.size())) return kDefault;
    return m_cells[index];
}

void CompareEngine::setBlinkIndex(int idx) {
    if (idx < -1 || idx >= imageCount()) return;
    m_blinkIndex = idx;
}

ImageData CompareEngine::differenceMap(int index, int baseIndex) {
    if (index < 0 || index >= imageCount()) return ImageData();
    if (baseIndex < 0 || baseIndex >= imageCount()) return ImageData();
    const QImage a = mvcore::toQImage(m_images[baseIndex].pixels());
    const QImage b = mvcore::toQImage(m_images[index].pixels());
    if (a.isNull() || b.isNull()) return ImageData();
    const QImage aa = a.convertToFormat(QImage::Format_RGB32);
    const QImage bb = b.convertToFormat(QImage::Format_RGB32);
    const int w = std::min(aa.width(), bb.width());
    const int h = std::min(aa.height(), bb.height());
    QImage out(w, h, QImage::Format_Grayscale8);
    if (out.isNull()) return ImageData();
    for (int y = 0; y < h; ++y) {
        const QRgb* la = reinterpret_cast<const QRgb*>(aa.constScanLine(y));
        const QRgb* lb = reinterpret_cast<const QRgb*>(bb.constScanLine(y));
        uchar* dst = out.scanLine(y);
        for (int x = 0; x < w; ++x) {
            const int dr = std::abs(static_cast<int>(qRed(la[x])) - qRed(lb[x]));
            const int dg = std::abs(static_cast<int>(qGreen(la[x])) - qGreen(lb[x]));
            const int db = std::abs(static_cast<int>(qBlue(la[x])) - qBlue(lb[x]));
            dst[x] = static_cast<uchar>(std::min(255, (dr + dg + db) / 3));
        }
    }
    return mvcore::fromQImage(out);
}
