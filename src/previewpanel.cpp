#include "previewpanel.h"

#include "core/image/Decoder.h"
#include "core/image/FrameSequence.h"
#include "core/image/ImageStats.h"
#include "core/image/QtConvert.h"

#include <QApplication>
#include <QFileInfo>
#include <QImageIOHandler>
#include <QImageReader>
#include <QMetaObject>
#include <QPainter>
#include <QPalette>
#include <QPointer>
#include <QResizeEvent>
#include <QSize>
#include <algorithm>
#include <utility>

PreviewPanel::PreviewPanel(QWidget *parent) : QWidget(parent)
{
    m_lifetime = mviewer::core::AsyncLifetimeToken::create();
    setMinimumSize(220, 220);
}

PreviewPanel::~PreviewPanel()
{
    m_lifetime->invalidate();
    cancelPending();
}

std::string PreviewPanel::previewCacheKey(const std::string &path)
{
    return mviewer::application::ImageLoadingService::instance().makeKey(path) + "#preview" +
           std::to_string(kPreviewMaxEdge);
}

namespace
{
QPixmap unpadSquareThumbnail(const QPixmap &pm, const QSize &knownSourceSize)
{
    if (pm.isNull() || pm.width() <= 0 || pm.height() <= 0)
        return pm;
    if (pm.width() != pm.height())
        return pm;

    const int s = pm.width();
    if (knownSourceSize.isValid() && knownSourceSize.width() > 0 && knownSourceSize.height() > 0)
    {
        const double aspect =
            static_cast<double>(knownSourceSize.width()) / knownSourceSize.height();
        if (aspect > 1.001)
        {
            const int ch = qMax(1, qRound(static_cast<double>(s) / aspect));
            const int y = (s - ch) / 2;
            return pm.copy(0, y, s, ch);
        }
        else if (aspect < 0.999)
        {
            const int cw = qMax(1, qRound(static_cast<double>(s) * aspect));
            const int x = (s - cw) / 2;
            return pm.copy(x, 0, cw, s);
        }
        return pm;
    }

    if (pm.hasAlpha())
    {
        const QImage img = pm.toImage();
        const int w = img.width();
        const int h = img.height();
        const int cx = w / 2;
        const int cy = h / 2;
        const bool hasTopBottomPadding =
            (qAlpha(img.pixel(cx, 0)) == 0) || (qAlpha(img.pixel(cx, h - 1)) == 0);
        const bool hasLeftRightPadding =
            (qAlpha(img.pixel(0, cy)) == 0) || (qAlpha(img.pixel(w - 1, cy)) == 0);

        if (hasTopBottomPadding && !hasLeftRightPadding)
        {
            int top = 0;
            while (top < h / 2 && qAlpha(img.pixel(cx, top)) == 0)
                ++top;
            int bottom = h - 1;
            while (bottom > h / 2 && qAlpha(img.pixel(cx, bottom)) == 0)
                --bottom;
            if (top > 0 || bottom < h - 1)
            {
                const int ch = qMax(1, bottom - top + 1);
                return pm.copy(0, top, w, ch);
            }
        }
        else if (hasLeftRightPadding && !hasTopBottomPadding)
        {
            int left = 0;
            while (left < w / 2 && qAlpha(img.pixel(left, cy)) == 0)
                ++left;
            int right = w - 1;
            while (right > w / 2 && qAlpha(img.pixel(right, cy)) == 0)
                --right;
            if (left > 0 || right < w - 1)
            {
                const int cw = qMax(1, right - left + 1);
                return pm.copy(left, 0, cw, h);
            }
        }
    }
    return pm;
}

struct PreviewDecodeResult
{
    QImage qimg;
    int srcW = 0;
    int srcH = 0;
    qint64 fileSize = 0;
    bool sourceKnown = false;
    bool fileSizeKnown = false;
    ImageData img;
};

PreviewDecodeResult loadPreviewPixels(const std::string &stdPath, int knownW, int knownH,
                                      qint64 knownSize)
{
    PreviewDecodeResult out;
    out.fileSize = knownSize >= 0 ? knownSize : 0;
    out.sourceKnown = knownW > 0 && knownH > 0;
    out.fileSizeKnown = knownSize >= 0;
    const std::string cacheKey = PreviewPanel::previewCacheKey(stdPath);
    mviewer::domain::ImageMetadata meta;
    ImageData img;
    if (mviewer::application::ImageLoadingService::instance().getPreviewCache(cacheKey, img))
    {
        // Warm scaled preview — reuse it.
    }
    else if (mviewer::core::FrameSequenceReader::isSequencePath(stdPath))
    {
        const auto decoded = mviewer::core::FrameSequenceReader::decodeFrameScaled(
            stdPath, 0, PreviewPanel::kPreviewMaxEdge);
        if (decoded.ok)
        {
            meta = decoded.metadata;
            img = decoded.pixels;
        }
        if (!img.isNull())
            mviewer::application::ImageLoadingService::instance().putPreviewCache(cacheKey, img);
    }
    else
    {
        ImageData decoded = Decoder::decodeScaled(stdPath, PreviewPanel::kPreviewMaxEdge, meta);
        img = mvcore::toDisplayImageData(decoded, meta);
        if (!img.isNull())
            mviewer::application::ImageLoadingService::instance().putPreviewCache(cacheKey, img);
    }
    if (!img.isNull())
    {
        QSize src(meta.width, meta.height);
        if (!src.isValid())
        {
            QImageReader reader(QString::fromStdString(stdPath));
            reader.setAutoTransform(true);
            src = reader.size();
            if (src.isValid())
            {
                switch (reader.transformation())
                {
                case QImageIOHandler::TransformationRotate90:
                case QImageIOHandler::TransformationRotate270:
                case QImageIOHandler::TransformationMirrorAndRotate90:
                case QImageIOHandler::TransformationFlipAndRotate90:
                    src = src.transposed();
                    break;
                default:
                    break;
                }
            }
        }
        if (!src.isValid())
        {
            out.srcW = knownW > 0 ? knownW : img.width;
            out.srcH = knownH > 0 ? knownH : img.height;
        }
        else
        {
            out.srcW = src.width();
            out.srcH = src.height();
            out.sourceKnown = true;
        }
    }
    if (out.fileSize == 0)
        out.fileSize = static_cast<qint64>(meta.fileSize);
    if (out.fileSize == 0)
        out.fileSize = QFileInfo(QString::fromStdString(stdPath)).size();
    out.fileSizeKnown = out.fileSize > 0;
    out.img = std::move(img);
    if (!out.img.isNull())
        out.qimg = mvcore::toQImage(out.img);
    return out;
}

} // namespace

void PreviewPanel::clearPreview()
{
    m_presentedPath.clear();
    m_quality = PresentationQuality::None;
    m_preview = QPixmap();
    m_scaled = QPixmap();
    m_hasImage = false;
    m_previewW = 0;
    m_previewH = 0;
    m_imgW = 0;
    m_imgH = 0;
    m_fileSize = 0;
    m_sourceDimensionsKnown = false;
    m_fileSizeKnown = false;
    m_lumMean = 0.0;
    m_rMean = m_gMean = m_bMean = 0;
    update();
}

void PreviewPanel::presentWarmThumbnail(const QString &path, const QPixmap &warmThumbnail,
                                        const QSize &knownSourceSize, qint64 knownFileSize)
{
    m_presentedPath = path;
    m_quality = PresentationQuality::Thumbnail;
    m_preview = unpadSquareThumbnail(warmThumbnail, knownSourceSize);
    m_previewW = m_preview.width();
    m_previewH = m_preview.height();
    m_imgW = knownSourceSize.width() > 0 ? knownSourceSize.width() : m_previewW;
    m_imgH = knownSourceSize.height() > 0 ? knownSourceSize.height() : m_previewH;
    m_fileSize = knownFileSize >= 0 ? knownFileSize : 0;
    m_sourceDimensionsKnown = knownSourceSize.width() > 0 && knownSourceSize.height() > 0;
    m_fileSizeKnown = knownFileSize >= 0;
    m_lumMean = 0.0;
    m_rMean = m_gMean = m_bMean = 0;
    m_hasImage = true;
    rebuild();
    update();
}

void PreviewPanel::deliverVisualPreview(const QImage &qimg, const QString &path, uint64_t gen,
                                        int srcW, int srcH, qint64 fileSize, bool sourceKnown,
                                        bool fileSizeKnown)
{
    if (!m_lifetime->isAlive())
        return;
    if (path != m_requestedPath || gen != m_requestGen)
        return;
    resetMatchingHandle(gen);
    if (qimg.isNull())
        return;
    m_preview = QPixmap::fromImage(qimg);
    if (m_preview.isNull())
        return;
    m_presentedPath = path;
    m_quality = PresentationQuality::Preview;
    m_hasImage = true;
    m_imgW = srcW;
    m_imgH = srcH;
    m_previewW = m_preview.width();
    m_previewH = m_preview.height();
    m_fileSize = fileSize;
    m_sourceDimensionsKnown = sourceKnown;
    m_fileSizeKnown = fileSizeKnown;
    rebuild();
    update();
}

void PreviewPanel::deliverPreviewStats(const mviewer::core::PreviewStats &stats,
                                       const QString &path, uint64_t gen)
{
    if (!m_lifetime->isAlive() || path != m_requestedPath || gen != m_requestGen)
        return;
    if (stats.valid)
    {
        m_lumMean = stats.lumMean;
        m_rMean = stats.rMean;
        m_gMean = stats.gMean;
        m_bMean = stats.bMean;
    }
    else
    {
        m_lumMean = 0.0;
        m_rMean = m_gMean = m_bMean = 0;
    }
    rebuild();
    update();
}

void PreviewPanel::decodePreviewWorker(
    const std::string &stdPath, const QString &path, uint64_t gen,
    const QPointer<PreviewPanel> &guard,
    const std::shared_ptr<mviewer::core::AsyncLifetimeToken> &lifetime, int knownW, int knownH,
    qint64 knownSize, const TaskScheduler::TaskContext &ctx)
{
    if (ctx.isCancelled())
        return;
    PreviewDecodeResult loaded = loadPreviewPixels(stdPath, knownW, knownH, knownSize);
    if (ctx.isCancelled())
        return;
    QMetaObject::invokeMethod(qApp,
                              [path, gen, guard, lifetime, qimg = loaded.qimg, srcW = loaded.srcW,
                               srcH = loaded.srcH, fileSize = loaded.fileSize,
                               sourceKnown = loaded.sourceKnown,
                               fileSizeKnown = loaded.fileSizeKnown]()
                              {
                                  PreviewPanel *panel = guard.data();
                                  if (!panel || !lifetime->isAlive())
                                      return;
                                  panel->deliverVisualPreview(qimg, path, gen, srcW, srcH, fileSize,
                                                              sourceKnown, fileSizeKnown);
                              });
    if (loaded.qimg.isNull())
        return;
    if (ctx.isCancelled())
        return;
    const mviewer::core::PreviewStats stats = mviewer::core::computePreviewStats(loaded.img);
    QMetaObject::invokeMethod(qApp,
                              [path, gen, guard, lifetime, stats]()
                              {
                                  PreviewPanel *panel = guard.data();
                                  if (!panel || !lifetime->isAlive())
                                      return;
                                  panel->deliverPreviewStats(stats, path, gen);
                              });
}

void PreviewPanel::setImage(const QString &path, const QPixmap &warmThumbnail,
                            const QSize &knownSourceSize, qint64 knownFileSize)
{
    m_requestedPath = path;
    cancelPending();
    ++m_requestGen;
    if (path.isEmpty())
    {
        // Clear synchronously when a folder changes. This prevents an old
        // decoded frame from remaining visible while the next directory is
        // still being scanned asynchronously.
        clearPreview();
        return;
    }

    // Stage 1: present an already-materialized gallery thumbnail immediately.
    if (!warmThumbnail.isNull())
        presentWarmThumbnail(path, warmThumbnail, knownSourceSize, knownFileSize);

    // Single scaled decode on the Decode pool. M27: QPointer + generation guard.
    const uint64_t gen = m_requestGen;
    const int knownW = knownSourceSize.width();
    const int knownH = knownSourceSize.height();
    const qint64 knownSize = knownFileSize;
    QPointer<PreviewPanel> guard(this);
    auto lifetime = m_lifetime;
    const std::string stdPath = path.toUtf8().toStdString();

    auto handle = TaskScheduler::instance().submit(
        TaskScheduler::Priority::Decode,
        [stdPath, path, gen, guard, lifetime, knownW, knownH,
         knownSize](const TaskScheduler::TaskContext &ctx)
        {
            decodePreviewWorker(stdPath, path, gen, guard, lifetime, knownW, knownH, knownSize,
                                ctx);
        });
    if (handle)
        m_task = handle;
}

void PreviewPanel::cancelPending()
{
    if (m_task)
        TaskScheduler::cancel(m_task);
    m_task.reset();
}

void PreviewPanel::resetMatchingHandle(uint64_t gen)
{
    // Only release the handle that delivered this result — a newer setImage()
    // may already have replaced m_task with its own generation's handle.
    if (gen == m_requestGen)
        m_task.reset();
}

void PreviewPanel::rebuild()
{
    if (m_preview.isNull())
        return;
    const int pad = 10;
    const int txtH = 56;
    const int availW = width() - pad * 2;
    const int availH = height() - pad * 2 - txtH;
    if (availW <= 0 || availH <= 0)
    {
        m_scaled = QPixmap();
        return;
    }
    const double s = std::min(static_cast<double>(availW) / m_preview.width(),
                              static_cast<double>(availH) / m_preview.height());
    m_scaled = m_preview.scaled(static_cast<int>(m_preview.width() * s),
                                static_cast<int>(m_preview.height() * s), Qt::KeepAspectRatio,
                                Qt::SmoothTransformation);
}

void PreviewPanel::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    if (m_hasImage)
        rebuild();
}

void PreviewPanel::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);
    QPainter painter(this);
    const QPalette pal = palette();
    const QColor base = pal.color(QPalette::Base);
    const QColor well = pal.color(QPalette::AlternateBase);
    const QColor text = pal.color(QPalette::Text);
    const QColor secondary = pal.color(QPalette::Mid);
    painter.fillRect(rect(), base);

    if (!m_hasImage)
    {
        painter.setPen(secondary);
        QFont f = font();
        f.setItalic(true);
        painter.setFont(f);
        painter.drawText(rect(), Qt::AlignCenter, "拖放图片或文件夹到此处\n或按 Ctrl+O 打开目录");
        return;
    }

    const int pad = 10;
    const int txtH = 56;
    const QRect imgArea(pad, pad, width() - pad * 2, height() - pad * 2 - txtH);
    if (!m_scaled.isNull())
    {
        const int x = imgArea.x() + (imgArea.width() - m_scaled.width()) / 2;
        const int y = imgArea.y() + (imgArea.height() - m_scaled.height()) / 2;
        painter.fillRect(QRect(x, y, m_scaled.width(), m_scaled.height()), well);
        painter.setPen(pal.color(QPalette::Mid));
        painter.drawRect(x - 1, y - 1, m_scaled.width() + 1, m_scaled.height() + 1);
        painter.drawPixmap(x, y, m_scaled);
    }

    const QRect txtArea(8, height() - txtH - 4, width() - 16, txtH);
    painter.setPen(text);
    QFont f = painter.font();
    f.setPointSize(9);
    painter.setFont(f);
    // M46: lexical fileName — paint never constructs QFileInfo.
    const QString pathStr = m_presentedPath;
    const int slash = qMax(pathStr.lastIndexOf('/'), pathStr.lastIndexOf('\\'));
    const QString name = slash >= 0 ? pathStr.mid(slash + 1) : pathStr;
    QString identity = name;
    if (m_sourceDimensionsKnown)
        identity += "\n" + QString::number(m_imgW) + "×" + QString::number(m_imgH);
    if (m_fileSizeKnown)
        identity +=
            (m_sourceDimensionsKnown ? "  " : "\n") + QString::number(m_fileSize / 1024) + " KB";
    painter.drawText(txtArea, Qt::AlignTop | Qt::AlignLeft, identity);
    // The brightness/RGB figures are sample means computed over the scaled
    // preview buffer, not the full image.
    painter.setPen(secondary);
    painter.drawText(txtArea.adjusted(0, 36, 0, 0), Qt::AlignTop | Qt::AlignLeft,
                     QString("样本亮度 %1   RGB(%2,%3,%4)")
                         .arg(m_lumMean, 0, 'f', 1)
                         .arg(m_rMean)
                         .arg(m_gMean)
                         .arg(m_bMean));
}
