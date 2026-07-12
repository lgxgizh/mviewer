#include "core/compare/CompareEngine.h"
#include "core/image/Decoder.h"
#include "core/scheduler/TaskScheduler.h"

#include <QImage>
#include <QPointF>
#include <QSize>
#include <cmath>

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

QPoint CompareLayout::cellPos(int index, const QSize &viewportSize) const
{
    if (imageCount <= 0) return QPoint(0, 0);
    const int cellW = viewportSize.width() / cols;
    const int cellH = viewportSize.height() / rows;
    const int c = index % cols;
    const int r = index / cols;
    return QPoint(c * cellW, r * cellH);
}

QSize CompareLayout::cellSize(const QSize &viewportSize) const
{
    if (imageCount <= 0) return viewportSize;
    return QSize(viewportSize.width() / cols, viewportSize.height() / rows);
}

CompareEngine::CompareEngine()
{
    m_layout = CompareLayout::forCount(0);
}

void CompareEngine::setImages(const QStringList &paths)
{
    m_images.clear();
    m_images.reserve(paths.size());
    for (const QString &p : paths) {
        // 解码到 Viewer 级缓存
        QImage img = Decoder::decodeFull(p);
        if (!img.isNull()) {
            m_images.emplace_back(p, img);
        }
    }
    rebuildLayout();
    m_blinkIndex = -1;
}

void CompareEngine::addImage(const QString &path)
{
    QImage img = Decoder::decodeFull(path);
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

void CompareEngine::setOffset(const QPointF &off)
{
    m_sync.offset = off;
}

void CompareEngine::zoomAt(const QPointF &viewPos, double factor, int exceptIndex)
{
    m_sync.scale *= factor;
    m_sync.scale = std::clamp(m_sync.scale, 0.05, 50.0);
    // offset 不变(保持视图中心); exceptIndex 预留给独立缩放某张
    Q_UNUSED(viewPos); Q_UNUSED(exceptIndex);
}

void CompareEngine::setBlinkIndex(int idx)
{
    m_blinkIndex = (idx >= 0 && idx < imageCount()) ? idx : -1;
}

QImage CompareEngine::differenceMap(int index, int baseIndex)
{
    if (index < 0 || index >= imageCount()) return QImage();
    if (baseIndex < 0 || baseIndex >= imageCount()) return QImage();
    const QImage &a = m_images[baseIndex].image();
    const QImage &b = m_images[index].image();
    if (a.isNull() || b.isNull()) return QImage();

    const int w = std::min(a.width(), b.width());
    const int h = std::min(a.height(), b.height());
    QImage out(w, h, QImage::Format_Grayscale8);
    if (out.isNull()) return QImage();

    for (int y = 0; y < h; ++y) {
        const QRgb *la = reinterpret_cast<const QRgb *>(a.constScanLine(y));
        const QRgb *lb = reinterpret_cast<const QRgb *>(b.constScanLine(y));
        uchar *dst = out.scanLine(y);
        for (int x = 0; x < w; ++x) {
            const int dr = abs(static_cast<int>(qRed(la[x])) - qRed(lb[x]));
            const int dg = abs(static_cast<int>(qGreen(la[x])) - qGreen(lb[x]));
            const int db = abs(static_cast<int>(qBlue(la[x])) - qBlue(lb[x]));
            dst[x] = static_cast<uchar>(std::min(255, (dr + dg + db) / 3));
        }
    }
    return out;
}
