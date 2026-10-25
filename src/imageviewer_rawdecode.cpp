#include "imageviewer.h"

#include "core/image/ImageFormats.h"
#include "core/image/decoder/RawDecodePlan.h"
#include "rawsourcebadge.h"

#include <QApplication>
#include <QFont>
#include <QFontMetrics>
#include <QMetaObject>
#include <QPainter>
#include <QPointer>

#include <atomic>
#include <memory>

namespace
{

void paintCornerBadge(QPainter &painter, const QRect &pane, const QString &text, bool right, int y)
{
    if (text.isEmpty() || pane.width() < 48 || pane.height() < 24)
        return;
    painter.save();
    QFont font = painter.font();
    font.setBold(true);
    font.setPointSize(9);
    painter.setFont(font);
    const int textW = painter.fontMetrics().horizontalAdvance(text);
    const int boxW = textW + 12;
    const int boxH = 18;
    const int x = right ? pane.right() - boxW - 5 : pane.left() + 6;
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(0, 0, 0, 170));
    painter.drawRoundedRect(x, y, boxW, boxH, 3, 3);
    painter.setPen(Qt::white);
    painter.drawText(QRect(x, y, boxW, boxH), Qt::AlignCenter, text);
    painter.restore();
}

} // namespace

void ImageViewer::drawRawSourceBadge(QPainter &painter)
{
    const int leftY = m_overlayMode == mviewer::OverlayMode::None ? 8 : 36;
    if (m_frame && m_frame->metadata().rawSource != mviewer::domain::RawSource::NotRaw)
    {
        const QString text =
            rawSourceBadgeText(m_frame->metadata().rawSource, m_frame->width(), m_frame->height());
        paintCornerBadge(painter, rect(), text, false, leftY);
    }
    if (m_fullRawLoading)
        paintCornerBadge(painter, rect(), tr("Loading"), true, 36);
}

void ImageViewer::stopFullRawDecode(bool notify)
{
    if (!m_fullRawLoading && !m_fullRawRequest)
        return;
    ++m_fullRawGen;
    mviewer::application::ImageLoadingService::instance().cancelAsync(m_fullRawRequest);
    if (!m_fullRawPath.empty())
        mviewer::core::clearRawFullDecode(m_fullRawPath);
    m_fullRawPath.clear();
    m_fullRawLoading = false;
    if (property("mviewerClosing").toBool())
        return;
    emit fullRawActivityChanged(false);
    if (notify)
        emit statusMessageRequested(tr("Full RAW decode cancelled"), 3000);
    update();
}

void ImageViewer::cancelFullRawDecode()
{
    stopFullRawDecode(true);
}

void ImageViewer::requestFullRawDecode()
{
    if (property("mviewerClosing").toBool())
        return;
    const QString path = m_currentPath;
    const std::string utf8 = path.toUtf8().toStdString();
    if (path.isEmpty() || !mviewer::core::isRawPath(utf8))
    {
        emit statusMessageRequested(tr("Decode Full RAW applies only to RAW images"), 3000);
        return;
    }
    if (m_frame && m_frame->metadata().rawSource == mviewer::domain::RawSource::FullDemosaic)
    {
        emit statusMessageRequested(tr("This image is already a full RAW decode"), 3000);
        return;
    }
    if (m_fullRawLoading && m_fullRawPath == utf8)
        return;
    if (m_fullRawLoading)
        stopFullRawDecode(false);

    mviewer::core::requestRawFullDecode(utf8);
    m_fullRawPath = utf8;
    m_fullRawLoading = true;
    const uint64_t generation = ++m_fullRawGen;
    emit fullRawActivityChanged(true);
    update();

    auto delivered = std::make_shared<std::atomic<bool>>(false);
    auto guard = std::make_shared<QPointer<ImageViewer>>(this);
    m_fullRawRequest = mviewer::application::ImageLoadingService::instance().loadAsyncCancellable(
        utf8,
        [guard, path, generation, delivered](const ImageLoadResult &result)
        {
            delivered->store(true, std::memory_order_release);
            QMetaObject::invokeMethod(
                qApp,
                [guard, path, generation, result]()
                {
                    ImageViewer *viewer = guard->data();
                    if (!viewer || viewer->property("mviewerClosing").toBool())
                        return;
                    viewer->applyFullRawResult(path, generation, result);
                },
                Qt::QueuedConnection);
        },
        mviewer::application::ImageLoadingService::defaultLoadOptions(), m_lifetime);
    if (!m_fullRawRequest && !delivered->load(std::memory_order_acquire))
    {
        mviewer::core::clearRawFullDecode(utf8);
        m_fullRawPath.clear();
        m_fullRawLoading = false;
        emit fullRawActivityChanged(false);
        emit statusMessageRequested(tr("Full RAW decode failed or was cancelled"), 4000);
        update();
    }
}

void ImageViewer::applyFullRawResult(const QString &path, uint64_t generation,
                                     const ImageLoadResult &result)
{
    if (generation != m_fullRawGen || path != m_currentPath)
        return;
    m_fullRawRequest.reset();
    m_fullRawLoading = false;
    m_loading = false;
    m_largeSourcePending = false;
    emit fullRawActivityChanged(false);
    const bool ok = result.success() && result.frame && !result.frame->pixels().isNull();
    if (!ok)
    {
        if (!m_fullRawPath.empty())
            mviewer::core::clearRawFullDecode(m_fullRawPath);
        m_fullRawPath.clear();
        emit statusMessageRequested(tr("Full RAW decode failed or was cancelled"), 4000);
        update();
        return;
    }

    m_lodMode = false;
    m_raster = DisplayRaster{};
    cancelDisplayRequest();
    m_foregroundRequest.reset();
    m_transitionImage = QImage();
    m_transitionSourceSize = QSize();
    m_provisionalPath.clear();
    m_provisionalImage = QImage();
    m_provisionalSourceSize = QSize();
    m_frame = result.frame;
    m_sequence = m_frame->sequenceInfo();
    m_frameIndex = m_frame->frameIndex();
    m_tiles = TileGrid(m_frame->width(), m_frame->height(), 256);
    m_tileCache.clear();
    m_overlayCache.clear();
    computeHistogram();
    clearLoadedGpu();
    emit statusMessageRequested(tr("Full RAW decode ready"), 3000);
    emit imageReady(m_frame);
    if (m_fitMode)
        zoomFit();
    else
        update();
}
