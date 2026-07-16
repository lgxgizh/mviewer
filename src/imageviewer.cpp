#include "imageviewer.h"

#include "core/analysis/AnalysisEngine.h"
#include "core/image/ImageRepository.h"
#include "core/image/QtConvert.h"
#include "core/render/RenderEngine.h"
#include "core/trace/Trace.h"

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
const QStringList kImageExtensions = {"*.jpg", "*.jpeg", "*.bmp", "*.png", "*.tif", "*.tiff"};
const double kZoomStep = 1.15;
} // namespace

QStringList ImageViewer::listImages(const QString &dirPath)
{
    QDir dir(dirPath);
    if (!dir.exists())
        return {};

    QFileInfoList entries = dir.entryInfoList(kImageExtensions, QDir::Files, QDir::Name);
    QStringList result;
    result.reserve(entries.size());
    for (const QFileInfo &info : entries)
        result.append(info.absoluteFilePath());
    return result;
}

ImageViewer::ImageViewer(QWidget *parent) : QWidget(parent)
{
    setWindowTitle("图片查看");
    setMouseTracking(true);
    setCursor(Qt::OpenHandCursor);
    setMinimumSize(200, 200);
}

ImageViewer::~ImageViewer() = default;

QPixmap ImageViewer::loadPixmap(const QString &path)
{
    // Acceptance #7: ALL image operations pass through ImageRepository.
    // The QWidget never decodes — it only renders the ImageFrame pixels.
    // ImageRepository serves pixels from the in-memory Viewer/FullImage LRU
    // after the first decode, so adjacent-image switching is instant.
    ImageRepository::Result res = ImageRepository::instance().load(path.toStdString());
    if (!res.success())
        return QPixmap();

    m_frame = res.frame;
    return QPixmap::fromImage(mvcore::toQImage(m_frame->pixels()));
}

void ImageViewer::setImage(const QString &path)
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

    // Build the render pipeline state: tile grid over the full-res image, and
    // a Viewport fitted into the widget. The Widget no longer keeps its own
    // scale/offset — pan/zoom math is delegated to core/render/Viewport.
    m_tiles = TileGrid(m_pixmap.width(), m_pixmap.height(), 256);
    m_view.screenW = width();
    m_view.screenH = height();
    m_view.fit(m_pixmap.width(), m_pixmap.height(), 0.95);
    preloadNeighbors(path);
    update();
}

void ImageViewer::preloadNeighbors(const QString &path)
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
    // Delegate the fit math to Viewport; keep the Widget free of scale/offset.
    m_view.screenW = width();
    m_view.screenH = height();
    m_view.fit(m_pixmap.width(), m_pixmap.height(), 0.95);
}

void ImageViewer::computeHistogram(const QPixmap &pixmap)
{
    Q_UNUSED(pixmap);
    std::fill(std::begin(m_histogram), std::end(m_histogram), 0);

    // Reuse the ImageFrame's cached luminance histogram (computed once on
    // decode inside ImageRepository). No re-decode in the QWidget layer.
    if (!m_frame || !m_frame->hasHistogram())
    {
        m_hasHistogram = false;
        return;
    }
    const auto &hist = m_frame->histogram();
    for (int i = 0; i < 256; ++i)
        m_histogram[i] = std::min(hist.luminance[i], 0x7FFFFFFF);
    m_hasHistogram = true;
}

void ImageViewer::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);

    QPainter painter(this);
    painter.fillRect(rect(), Qt::black);

    // Render Pipeline (P1-①): the Widget is the Viewport owner. It asks the
    // TileGrid which source tiles are visible, then asks the Renderer to scale
    // each visible region from the ImageFrame pixels — never decoding, never
    // rasterizing the whole image into one bitmap. This is what makes 100MP/
    // RAW rendering feasible later.
    if (!m_pixmap.isNull() && m_frame)
    {
        MV_TRACE_SCOPED("ImageViewer::paint");
        m_view.screenW = width();
        m_view.screenH = height();
        auto tiles = m_tiles.visibleTiles(m_view);
        RenderEngine &eng = RenderEngine::instance();
        for (const auto &t : tiles)
        {
            int sx, sy, sw, sh;
            m_view.imageRectToScreen(t.srcX, t.srcY, t.srcW, t.srcH, sx, sy, sw, sh);
            const RenderRect region{t.srcX, t.srcY, t.srcW, t.srcH};
            const RenderSize tgt{sw, sh};
            ImageData regionImg = eng.scaleRegion(m_frame->pixels(), region, tgt,
                                                  m_view.scale < 1.0 ? RenderInterp::Bilinear
                                                                     : RenderInterp::Nearest);
            if (regionImg.isNull())
                continue;
            QImage q = mvcore::toQImage(regionImg);
            if (q.isNull())
                continue;
            painter.drawImage(QRect(sx, sy, sw, sh), q);
        }
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

void ImageViewer::drawHistogram(QPainter &painter) const
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

void ImageViewer::wheelEvent(QWheelEvent *event)
{
    if (m_pixmap.isNull())
        return;

    m_view.screenW = width();
    m_view.screenH = height();
    const QPointF mouse = event->position();
    const double factor = event->angleDelta().y() > 0 ? kZoomStep : 1.0 / kZoomStep;
    m_view.zoomAt(mouse.x(), mouse.y(), factor);
    update();
}

void ImageViewer::mousePressEvent(QMouseEvent *event)
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

void ImageViewer::mouseMoveEvent(QMouseEvent *event)
{
    if (m_selecting)
    {
        m_selEnd = event->pos();
        update();
    }
    else if (m_dragging)
    {
        m_view.pan(event->pos().x() - m_lastMousePos.x(), event->pos().y() - m_lastMousePos.y());
        m_lastMousePos = event->pos();
        update();
    }

    // Pixel Inspector (P1 #6): read the pixel under the cursor directly from
    // the ImageFrame (RGB24), using the inverse of the Viewport transform.
    int ix = -1, iy = -1, r = 0, g = 0, b = 0;
    bool valid = false;
    if (m_frame && !m_pixmap.isNull())
    {
        m_view.screenW = width();
        m_view.screenH = height();
        const double imgX = (event->pos().x() - m_view.offsetX) / m_view.scale;
        const double imgY = (event->pos().y() - m_view.offsetY) / m_view.scale;
        ix = static_cast<int>(std::floor(imgX));
        iy = static_cast<int>(std::floor(imgY));
        const int iw = m_pixmap.width();
        const int ih = m_pixmap.height();
        if (ix >= 0 && ix < iw && iy >= 0 && iy < ih)
        {
            const ImageBuffer view = m_frame->pixels().view();
            const uint8_t *p = view.data + static_cast<size_t>(iy) * view.stride() +
                               static_cast<size_t>(ix) * view.channelsPerPixel();
            r = p[0];
            g = p[1];
            b = p[2];
            valid = true;
        }
    }
    emit pixelInfo(ix, iy, r, g, b, valid);
}

void ImageViewer::mouseReleaseEvent(QMouseEvent *event)
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
                const QRect imgRect =
                    QRect(static_cast<int>((r.x() - m_view.offsetX) / m_view.scale),
                          static_cast<int>((r.y() - m_view.offsetY) / m_view.scale),
                          static_cast<int>(r.width() / m_view.scale),
                          static_cast<int>(r.height() / m_view.scale))
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

void ImageViewer::resizeEvent(QResizeEvent *event)
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

void ImageViewer::keyPressEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Left)
        emit requestPrev();
    else if (event->key() == Qt::Key_Right)
        emit requestNext();
    else
        QWidget::keyPressEvent(event);
}
