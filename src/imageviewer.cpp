#include "imageviewer.h"

#include "core/analysis/AnalysisEngine.h"
#include "core/cache/CacheManager.h"
#include "core/image/Decoder.h"
#include "core/image/QtConvert.h"

#include <QApplication>
#include <QDir>
#include <QFileInfo>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QRect>
#include <QResizeEvent>
#include <QWheelEvent>

namespace
{
const QStringList kImageExtensions = {"*.jpg", "*.jpeg", "*.bmp", "*.png"};
const double kZoomStep = 1.15;
} // namespace

QStringList ImageViewer::listImages(const QString& dirPath)
{
    QDir dir(dirPath);
    if (!dir.exists())
        return {};

    QFileInfoList entries = dir.entryInfoList(kImageExtensions, QDir::Files, QDir::Name);
    QStringList result;
    result.reserve(entries.size());
    for (const QFileInfo& info : entries)
        result.append(info.absoluteFilePath());
    return result;
}

ImageViewer::ImageViewer(QWidget* parent)
    : QWidget(parent)
{
    setWindowTitle("图片查看");
    setMouseTracking(true);
    setCursor(Qt::OpenHandCursor);
    setMinimumSize(200, 200);
}

ImageViewer::~ImageViewer() = default;

QPixmap ImageViewer::loadPixmap(const QString& path)
{
    auto it = m_cache.find(path);
    if (it != m_cache.end())
    {
        m_cacheOrder.erase(std::find(m_cacheOrder.begin(), m_cacheOrder.end(), path));
        m_cacheOrder.push_front(path);
        return it->second;
    }

    // 尝试从 CacheManager 获取已解码的图像（省去二次磁盘IO）
    QPixmap pix;
    ImageData cached;
    if (CacheManager::instance().getMemory(CacheLevel::FullImage, path.toStdString(), cached))
    {
        pix = QPixmap::fromImage(mvcore::toQImage(cached));
    }
    if (pix.isNull())
    {
        // 未命中：解码并写入 CacheManager
        ImageData decoded = Decoder::decodeFull(path.toStdString());
        if (!decoded.isNull())
        {
            CacheManager::instance().put(CacheLevel::FullImage, path.toStdString(), decoded);
            pix = QPixmap::fromImage(mvcore::toQImage(decoded));
        }
    }
    // 兜底：用 Decoder 解码（仍走 Encoder/Decoder 统一路径）
    if (pix.isNull())
    {
        ImageData decoded = Decoder::decodeFull(path.toStdString());
        if (!decoded.isNull())
        {
            pix = QPixmap::fromImage(mvcore::toQImage(decoded));
        }
    }

    m_cacheOrder.push_front(path);
    m_cache[path] = pix;
    while (static_cast<int>(m_cacheOrder.size()) > kMaxCache)
    {
        const QString victim = m_cacheOrder.back();
        m_cacheOrder.pop_back();
        m_cache.erase(victim);
    }

    return pix;
}

void ImageViewer::setImage(const QString& path)
{
    m_currentPath = path;
    m_pixmap = loadPixmap(path);

    if (m_pixmap.isNull())
    {
        m_hasHistogram = false;
        update();
        return;
    }

    computeHistogram(m_pixmap);

    const QFileInfo info(path);
    m_fileList = listImages(info.absolutePath());
    m_currentIndex = m_fileList.indexOf(path);

    fitToWidget();
    preloadNeighbors(path);
    update();
}

void ImageViewer::preloadNeighbors(const QString& path)
{
    if (m_currentIndex < 0)
        return;

    for (int delta = -1; delta <= 1; ++delta)
    {
        const int i = m_currentIndex + delta;
        if (i < 0 || i >= m_fileList.size())
            continue;
        if (m_fileList[i] == path)
            continue;
        loadPixmap(m_fileList[i]);
    }
}

void ImageViewer::fitToWidget()
{
    if (m_pixmap.isNull())
        return;

    const double sx = static_cast<double>(width()) / m_pixmap.width();
    const double sy = static_cast<double>(height()) / m_pixmap.height();
    m_scale = std::min(sx, sy) * 0.95;
    if (m_scale <= 0.0)
        m_scale = 1.0;

    m_offset = QPointF((width() - m_pixmap.width() * m_scale) / 2.0,
        (height() - m_pixmap.height() * m_scale) / 2.0);
}

void ImageViewer::computeHistogram(const QPixmap& pixmap)
{
    std::fill(std::begin(m_histogram), std::end(m_histogram), 0);

    const QImage image = pixmap.toImage().convertToFormat(QImage::Format_RGB32);
    if (image.isNull())
    {
        m_hasHistogram = false;
        return;
    }

    const int w = image.width();
    const int h = image.height();
    for (int y = 0; y < h; ++y)
    {
        const QRgb* line = reinterpret_cast<const QRgb*>(image.scanLine(y));
        for (int x = 0; x < w; ++x)
        {
            const QRgb c = line[x];
            const int lum =
                static_cast<int>(0.299 * qRed(c) + 0.587 * qGreen(c) + 0.114 * qBlue(c));
            ++m_histogram[std::clamp(lum, 0, 255)];
        }
    }
    m_hasHistogram = true;
}

void ImageViewer::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event);

    QPainter painter(this);
    painter.fillRect(rect(), Qt::black);

    if (!m_pixmap.isNull())
    {
        painter.setRenderHint(QPainter::SmoothPixmapTransform);
        const QSize scaled = m_pixmap.size() * m_scale;
        painter.drawPixmap(
            QRectF(m_offset, scaled), m_pixmap, QRectF(QPointF(0, 0), m_pixmap.size()));
    }
    else if (!m_currentPath.isEmpty())
    {
        painter.setPen(Qt::white);
        painter.drawText(rect(), Qt::AlignCenter, "无法加载图片");
    }

    if (m_hasHistogram)
        drawHistogram(painter);

    if (m_selectMode)
    {
        const QRect r = m_selecting ? QRect(m_selStart, m_selEnd).normalized() : selectedRegion();
        if (r.isValid() && (m_selecting || r.width() > 5) && (m_selecting || r.height() > 5))
        {
            painter.save();
            painter.setPen(QPen(QColor(255, 255, 0), 1));
            painter.setBrush(QColor(255, 255, 0, 80));
            painter.drawRect(r);
            painter.restore();
        }
    }
}

void ImageViewer::drawHistogram(QPainter& painter) const
{
    const int w = 160;
    const int h = 90;
    const int margin = 10;
    const QRect bg(margin, margin, w, h);

    painter.save();
    painter.setBrush(QColor(0, 0, 0, 140));
    painter.setPen(Qt::NoPen);
    painter.drawRoundedRect(bg.adjusted(-4, -4, 4, 4), 6, 6);

    int maxVal = 1;
    for (int i = 0; i < 256; ++i)
        maxVal = std::max(maxVal, m_histogram[i]);

    painter.setPen(QColor(255, 255, 255, 200));
    painter.setBrush(Qt::NoBrush);
    const double dx = static_cast<double>(w) / 256.0;
    const double dy = static_cast<double>(h) / maxVal;

    QPointF prev;
    for (int i = 0; i < 256; ++i)
    {
        const double x = bg.left() + i * dx;
        const double y = bg.bottom() - m_histogram[i] * dy;
        const QPointF cur(x, y);
        if (i > 0)
            painter.drawLine(prev, cur);
        prev = cur;
    }
    painter.restore();
}

void ImageViewer::wheelEvent(QWheelEvent* event)
{
    if (m_pixmap.isNull())
        return;

    const QPointF mouse = event->position();

    const double factor = event->angleDelta().y() > 0 ? kZoomStep : 1.0 / kZoomStep;

    const QPointF imagePos = (mouse - m_offset) / m_scale;
    m_scale *= factor;
    m_scale = std::clamp(m_scale, 0.05, 50.0);
    m_offset = mouse - imagePos * m_scale;

    update();
}

void ImageViewer::mousePressEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton && !m_pixmap.isNull())
    {
        if (m_selectMode)
        {
            m_selecting = true;
            m_selStart = m_selEnd = event->pos();
        }
        else
        {
            m_dragging = true;
            m_lastMousePos = event->pos();
            setCursor(Qt::ClosedHandCursor);
        }
    }
}

void ImageViewer::mouseMoveEvent(QMouseEvent* event)
{
    if (m_selecting)
    {
        m_selEnd = event->pos();
        update();
    }
    else if (m_dragging)
    {
        m_offset += (event->pos() - m_lastMousePos);
        m_lastMousePos = event->pos();
        update();
    }
}

void ImageViewer::mouseReleaseEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton)
    {
        if (m_selecting)
        {
            m_selecting = false;
            const QRect r = selectedRegion();
            if (r.width() > 5 && r.height() > 5)
            {
                QImage img = m_pixmap.toImage().convertToFormat(QImage::Format_RGB32);
                const QRect imgRect = QRect(static_cast<int>((r.x() - m_offset.x()) / m_scale),
                    static_cast<int>((r.y() - m_offset.y()) / m_scale),
                    static_cast<int>(r.width() / m_scale),
                    static_cast<int>(r.height() / m_scale))
                                          .normalized();
                const QRect valid = imgRect.intersected(QRect(0, 0, img.width(), img.height()));
                if (!valid.isEmpty())
                {
                    const ImageStats stats =
                        AnalysisEngine::computeStats(mvcore::fromQImage(img.copy(valid)));
                    const QString text = QString("框选 [%1,%2,%3,%4]: 亮度=%5, R=%6,G=%7,B=%8")
                                             .arg(valid.x())
                                             .arg(valid.y())
                                             .arg(valid.width())
                                             .arg(valid.height())
                                             .arg(stats.lumMean, 0, 'f', 1)
                                             .arg(stats.rMean, 0, 'f', 1)
                                             .arg(stats.gMean, 0, 'f', 1)
                                             .arg(stats.bMean, 0, 'f', 1);
                    emit regionStats(text);
                    emit selectionChanged(valid); // new: live ROI stats
                }
                else
                {
                    emit selectionChanged(QRect());
                }
            }
        }
        else
        {
            m_dragging = false;
            setCursor(Qt::OpenHandCursor);
        }
    }
}

void ImageViewer::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    if (m_currentIndex < 0 && !m_pixmap.isNull())
        fitToWidget();
}

QRect ImageViewer::selectedRegion() const
{
    return QRect(m_selStart, m_selEnd).normalized();
}

void ImageViewer::setSelectMode(bool on)
{
    m_selectMode = on;
    setCursor(on ? Qt::CrossCursor : Qt::OpenHandCursor);
    update();
}

void ImageViewer::keyPressEvent(QKeyEvent* event)
{
    if (event->key() == Qt::Key_Left)
        emit requestPrev();
    else if (event->key() == Qt::Key_Right)
        emit requestNext();
    else
        QWidget::keyPressEvent(event);
}
