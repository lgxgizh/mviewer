#include "core/compare/CompareEngine.h"
#include "core/image/Decoder.h"
#include "core/image/QtConvert.h"
#include "core/scheduler/TaskScheduler.h"

#include <QImage>
#include <algorithm>
#include <cmath>
#include <cstring>

// 内部实现：像素级差异计算用 QImage（经 QtConvert 转换）。
// header 不暴露 Qt；这里在 .cpp 内部使用 Qt 作为实现细节。

CompareLayout CompareLayout::forCount(int n)
{
    CompareLayout l;
    l.imageCount = n;
    if (n <= 1) { l.cols = l.rows = 1; return l; }
    switch (n) {
    case 2: l.cols = 2; l.rows = 1; break;
    case 3: l.cols = 3; l.rows = 1; break;
    case 4: l.cols = 2; l.rows = 2; break;
    default: // 5-8
        l.cols = 4; l.rows = 2; break;
    }
    return l;
}

CellPoint CompareLayout::cellPos(int index, const CellSize &viewport) const
{
    if (imageCount <= 0) return CellPoint{0, 0};
    const int cellW = viewport.w / cols;
    const int cellH = viewport.h / rows;
    const int c = index % cols;
    const int r = index / cols;
    return CellPoint{c * cellW, r * cellH};
}

CellSize CompareLayout::cellSize(const CellSize &viewport) const
{
    if (imageCount <= 0) return viewport;
    return CellSize{viewport.w / cols, viewport.h / rows};
}

CompareEngine::CompareEngine()
{
    m_layout = CompareLayout::forCount(0);
}

void CompareEngine::setImages(const std::vector<std::string> &paths)
{
    m_images.clear();
    m_images.reserve(paths.size());
    for (const std::string &p : paths) {
        // 解码到 Viewer 级
        ImageData img = Decoder::decodeFull(p);
        if (!img.isNull()) {
            m_images.emplace_back(p, img);
        }
    }
    rebuildLayout();
    m_blinkIndex = -1;
}

void CompareEngine::addImage(const std::string &path)
{
    ImageData img = Decoder::decodeFull(path);
    if (!img.isNull()) {
        m_images.emplace_back(path, img);
        rebuildLayout();
    }
}

void CompareEngine::removeImage(int index)
{
    if (index < 0 || index >= static_cast<int>(m_images.size())) return;
    m_images.erase(m_images.begin() + index);
    rebuildLayout();
}

void CompareEngine::clear()
{
    m_images.clear();
    rebuildLayout();
    m_blinkIndex = -1;
}

const ImageObject *CompareEngine::imageAt(int index) const
{
    if (index < 0 || index >= static_cast<int>(m_images.size())) return nullptr;
    return &m_images[index];
}

void CompareEngine::rebuildLayout()
{
    m_layout = CompareLayout::forCount(imageCount());
}

void CompareEngine::setScale(double s)
{
    m_sync.scale = s;
}

void CompareEngine::setOffset(double ox, double oy)
{
    m_sync.offset = Vec2{ox, oy};
}

void CompareEngine::zoomAt(double viewX, double viewY, double factor, int exceptIndex)
{
    m_sync.scale *= factor;
    m_sync.scale = std::clamp(m_sync.scale, 0.05, 50.0);
    // offset 不变(保持视图中心); viewX/viewY/exceptIndex 预留给独立缩放某张
    (void)viewX; (void)viewY; (void)exceptIndex;
}

void CompareEngine::setBlinkIndex(int idx)
{
    m_blinkIndex = (idx >= 0 && idx < imageCount()) ? idx : -1;
}

ImageData CompareEngine::differenceMap(int index, int baseIndex)
{
    if (index < 0 || index >= imageCount()) return ImageData();
    if (baseIndex < 0 || baseIndex >= imageCount()) return ImageData();
    const QImage a = mvcore::toQImage(m_images[baseIndex].image());
    const QImage b = mvcore::toQImage(m_images[index].image());
    if (a.isNull() || b.isNull()) return ImageData();

    const QImage aa = a.convertToFormat(QImage::Format_RGB32);
    const QImage bb = b.convertToFormat(QImage::Format_RGB32);
    const int w = std::min(aa.width(), bb.width());
    const int h = std::min(aa.height(), bb.height());
    QImage out(w, h, QImage::Format_Grayscale8);
    if (out.isNull()) return ImageData();

    for (int y = 0; y < h; ++y) {
        const QRgb *la = reinterpret_cast<const QRgb *>(aa.constScanLine(y));
        const QRgb *lb = reinterpret_cast<const QRgb *>(bb.constScanLine(y));
        uchar *dst = out.scanLine(y);
        for (int x = 0; x < w; ++x) {
            const int dr = abs(static_cast<int>(qRed(la[x])) - qRed(lb[x]));
            const int dg = abs(static_cast<int>(qGreen(la[x])) - qGreen(lb[x]));
            const int db = abs(static_cast<int>(qBlue(la[x])) - qBlue(lb[x]));
            dst[x] = static_cast<uchar>(std::min(255, (dr + dg + db) / 3));
        }
    }
    return mvcore::fromQImage(out);
}
