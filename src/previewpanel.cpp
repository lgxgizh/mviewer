#include "previewpanel.h"

#include "SquareLetterbox.h"
#include "Theme.h"
#include "core/image/Decoder.h"
#include "core/image/FrameSequence.h"
#include "core/image/ImageStats.h"
#include "core/image/QtConvert.h"

#include <QApplication>
#include <QFileInfo>
#include <QImage>
#include <QImageIOHandler>
#include <QImageReader>
#include <QMetaObject>
#include <QPainter>
#include <QPalette>
#include <QPointer>
#include <QResizeEvent>
#include <QSize>
#include <QVariantAnimation>
#include <algorithm>
#include <cmath>
#include <utility>

namespace
{
// Same-path stage cross-fade (thumb -> preview, or the one frame correction
// when a placeholder had the wrong shape). Path changes are always instant.
constexpr int kStageTransitionMs = 110;
// A new stage for the same path keeps the current frame unless its source
// aspect differs by more than this (rounding / crop noise is not a change).
constexpr double kFrameAspectTolerance = 0.015;
// Beyond this pixmap-vs-frame aspect mismatch a stage is fitted (letterboxed
// on the well) instead of center-cropped, so a sideways placeholder is never
// reduced to a thin slice.
constexpr double kMaxCropAspectRatio = 1.3;

bool validSize(const QSize &s)
{
    return s.isValid() && s.width() > 0 && s.height() > 0;
}

double aspectOf(const QSize &s)
{
    return validSize(s) ? static_cast<double>(s.width()) / static_cast<double>(s.height()) : 0.0;
}
} // namespace

PreviewPanel::PreviewPanel(QWidget *parent) : QWidget(parent)
{
    m_lifetime = mviewer::core::AsyncLifetimeToken::create();
    setMinimumSize(220, 220);
    m_transition = new QVariantAnimation(this);
    m_transition->setStartValue(0.0);
    m_transition->setEndValue(1.0);
    m_transition->setDuration(kStageTransitionMs);
    m_transition->setEasingCurve(QEasingCurve::OutCubic);
    connect(m_transition, &QVariantAnimation::valueChanged, this,
            [this](const QVariant &v)
            {
                m_transitionT = v.toDouble();
                update();
            });
    connect(m_transition, &QVariantAnimation::finished, this,
            [this]()
            {
                m_fadeFrom = QPixmap();
                m_fadeFromFrame = QSize();
                m_transitionT = 1.0;
                update();
            });
}

PreviewPanel::~PreviewPanel()
{
    m_lifetime->invalidate();
    if (m_transition)
    {
        m_transition->disconnect(this);
        m_transition->stop();
    }
    cancelPending();
}

void PreviewPanel::setDevicePixelRatioForTest(qreal dpr)
{
    m_dprOverride = dpr;
    if (m_hasImage)
        rebuild();
    update();
}

bool PreviewPanel::isTransitioningForTest() const
{
    return transitionRunning();
}

bool PreviewPanel::transitionRunning() const
{
    return m_transition && m_transition->state() == QAbstractAnimation::Running;
}

qreal PreviewPanel::effectiveDevicePixelRatio() const
{
    const qreal dpr = m_dprOverride > 0.0 ? m_dprOverride : devicePixelRatioF();
    return dpr > 0.0 ? dpr : 1.0;
}

QSize PreviewPanel::resolveSourceSize(const QString &path, const QSize &knownSourceSize) const
{
    if (validSize(knownSourceSize))
        return knownSourceSize;
    const auto it = m_knownSources.constFind(path);
    if (it != m_knownSources.constEnd() && validSize(it->size))
        return it->size;
    return {};
}

void PreviewPanel::rememberSource(const QString &path, const QSize &size,
                                  const std::string &previewKey)
{
    if (path.isEmpty() || !validSize(size))
        return;
    auto it = m_knownSources.find(path);
    if (it != m_knownSources.end())
    {
        it->size = size;
        if (!previewKey.empty())
            it->previewKey = previewKey;
        return;
    }
    m_knownSources.insert(path, KnownSource{size, previewKey});
    m_knownSourceOrder.push_back(path);
    while (static_cast<int>(m_knownSourceOrder.size()) > kKnownSourceCap)
    {
        m_knownSources.remove(m_knownSourceOrder.front());
        m_knownSourceOrder.pop_front();
    }
}

bool PreviewPanel::applyFrameAspect(double aspect, bool samePath)
{
    if (!(aspect > 0.0) || !std::isfinite(aspect))
        return false;
    if (m_frameAspect > 0.0)
    {
        if (aspect == m_frameAspect)
            return false;
        if (samePath && std::abs(aspect / m_frameAspect - 1.0) < kFrameAspectTolerance)
            return false;
    }
    m_frameAspect = aspect;
    ++m_frameChanges;
    return true;
}

void PreviewPanel::beginStageTransition(bool samePath)
{
    const bool animate = samePath && m_hasImage && !m_scaled.isNull() && isVisible();
    const QSize from = displayedFrameSize();
    m_transition->stop();
    if (!animate)
    {
        m_fadeFrom = QPixmap();
        m_fadeFromFrame = QSize();
        m_transitionT = 1.0;
        return;
    }
    m_fadeFrom = m_scaled;
    m_fadeFromFrame = from;
    m_transitionT = 0.0;
    m_transition->start();
}

QSize PreviewPanel::displayedFrameSize() const
{
    if (!transitionRunning() || !validSize(m_fadeFromFrame) || !validSize(m_frameSize))
        return m_frameSize;
    const double t = std::clamp(m_transitionT, 0.0, 1.0);
    return QSize(
        qRound(m_fadeFromFrame.width() + (m_frameSize.width() - m_fadeFromFrame.width()) * t),
        qRound(m_fadeFromFrame.height() + (m_frameSize.height() - m_fadeFromFrame.height()) * t));
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
        if (aspect < 0.999)
        {
            const int cw = qMax(1, qRound(static_cast<double>(s) * aspect));
            const int x = (s - cw) / 2;
            return pm.copy(x, 0, cw, s);
        }
        return pm;
    }

    const QImage cropped = mviewer::ui::cropSquareLetterbox(pm.toImage());
    if (cropped.isNull() || cropped.size() == pm.size())
        return pm;
    return QPixmap::fromImage(cropped);
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
    std::string cacheKey;
};

PreviewDecodeResult loadPreviewPixels(const std::string &stdPath, int knownW, int knownH,
                                      qint64 knownSize)
{
    PreviewDecodeResult out;
    out.fileSize = knownSize >= 0 ? knownSize : 0;
    out.sourceKnown = knownW > 0 && knownH > 0;
    out.fileSizeKnown = knownSize >= 0;
    const std::string cacheKey = PreviewPanel::previewCacheKey(stdPath);
    out.cacheKey = cacheKey;
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
        out.qimg = mvcore::toQImageFast(out.img);
    return out;
}

} // namespace

void PreviewPanel::clearPreview()
{
    m_presentedPath.clear();
    m_quality = PresentationQuality::None;
    m_preview = QPixmap();
    m_scaled = QPixmap();
    m_transition->stop();
    m_fadeFrom = QPixmap();
    m_fadeFromFrame = QSize();
    m_transitionT = 1.0;
    m_frameAspect = 0.0;
    m_frameSize = QSize();
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
    const bool samePath = m_hasImage && m_presentedPath == path;
    beginStageTransition(samePath);
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
    if (m_sourceDimensionsKnown)
        rememberSource(path, knownSourceSize, std::string());
    // Frame = source display aspect when known; otherwise the unpadded thumb.
    applyFrameAspect(
        m_sourceDimensionsKnown ? aspectOf(knownSourceSize) : aspectOf(m_preview.size()), samePath);
    rebuild();
    update();
}

bool PreviewPanel::presentCachedPreview(const QString &path, const QSize &knownSourceSize,
                                        qint64 knownFileSize)
{
    // Only with a known source size (frame must not guess) and a preview key
    // this panel already learned for the path: no file I/O on the UI thread.
    if (!validSize(knownSourceSize))
        return false;
    const auto it = m_knownSources.constFind(path);
    if (it == m_knownSources.constEnd() || it->previewKey.empty())
        return false;
    ImageData img;
    if (!mviewer::application::ImageLoadingService::instance().getPreviewCache(it->previewKey,
                                                                               img) ||
        img.isNull())
        return false;
    const QPixmap pm = QPixmap::fromImage(mvcore::toQImageFast(img));
    if (pm.isNull())
        return false;
    const bool samePath = m_hasImage && m_presentedPath == path;
    beginStageTransition(samePath);
    m_presentedPath = path;
    m_quality = PresentationQuality::Preview;
    m_preview = pm;
    m_previewW = m_preview.width();
    m_previewH = m_preview.height();
    m_imgW = knownSourceSize.width();
    m_imgH = knownSourceSize.height();
    m_sourceDimensionsKnown = true;
    m_fileSize = knownFileSize >= 0 ? knownFileSize : 0;
    m_fileSizeKnown = knownFileSize >= 0;
    m_lumMean = 0.0;
    m_rMean = m_gMean = m_bMean = 0;
    m_hasImage = true;
    applyFrameAspect(aspectOf(knownSourceSize), samePath);
    rebuild();
    update();
    return true;
}

void PreviewPanel::deliverVisualPreview(const QImage &qimg, const QString &path, uint64_t gen,
                                        int srcW, int srcH, qint64 fileSize, bool sourceKnown,
                                        bool fileSizeKnown, const std::string &previewKey)
{
    if (!m_lifetime->isAlive())
        return;
    if (path != m_requestedPath || gen != m_requestGen)
        return;
    resetMatchingHandle(gen);
    if (qimg.isNull())
        return;
    const QPixmap pm = QPixmap::fromImage(qimg);
    if (pm.isNull())
        return;
    if (sourceKnown)
        rememberSource(path, QSize(srcW, srcH), previewKey);
    const bool samePath = m_hasImage && m_presentedPath == path;
    // Already sharp (cache hit shown synchronously, or a same-path
    // re-select): refresh pixels and metadata in place, no cross-fade.
    if (!(samePath && m_quality == PresentationQuality::Preview))
        beginStageTransition(samePath);
    m_preview = pm;
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
    applyFrameAspect(
        srcW > 0 && srcH > 0 ? aspectOf(QSize(srcW, srcH)) : aspectOf(m_preview.size()), samePath);
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
    // Text-only change: the frame and pixels stay untouched.
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
    QMetaObject::invokeMethod(
        qApp,
        [path, gen, guard, lifetime, qimg = loaded.qimg, srcW = loaded.srcW, srcH = loaded.srcH,
         fileSize = loaded.fileSize, sourceKnown = loaded.sourceKnown,
         fileSizeKnown = loaded.fileSizeKnown, previewKey = loaded.cacheKey]()
        {
            PreviewPanel *panel = guard.data();
            if (!panel || !lifetime->isAlive())
                return;
            panel->deliverVisualPreview(qimg, path, gen, srcW, srcH, fileSize, sourceKnown,
                                        fileSizeKnown, previewKey);
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

void PreviewPanel::offerWarmThumbnail(const QString &path, const QPixmap &warmThumbnail,
                                      const QSize &knownSourceSize, qint64 knownFileSize)
{
    if (path.isEmpty() || warmThumbnail.isNull() || path != m_requestedPath)
        return;
    if (m_quality == PresentationQuality::Preview && m_presentedPath == path)
        return;
    presentWarmThumbnail(path, warmThumbnail, resolveSourceSize(path, knownSourceSize),
                         knownFileSize);
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

    // Stage 1: show the best pixels available without waiting. A same-path
    // re-select that is already sharp is left alone (no downgrade to the thumb);
    // a memory-cached preview with a known source size goes straight to the
    // Preview stage; otherwise an already-materialized gallery thumbnail.
    const QSize resolvedSource = resolveSourceSize(path, knownSourceSize);
    const bool alreadySharp =
        m_hasImage && m_presentedPath == path && m_quality == PresentationQuality::Preview;
    if (!alreadySharp && !presentCachedPreview(path, resolvedSource, knownFileSize) &&
        !warmThumbnail.isNull())
        presentWarmThumbnail(path, warmThumbnail, resolvedSource, knownFileSize);

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

void PreviewPanel::releaseSourceHandles(const QString &path)
{
    if (path.isEmpty())
        return;
    mviewer::core::FrameSequenceReader::releaseFile(path.toUtf8().toStdString());
    if (m_requestedPath == path || m_presentedPath == path)
        cancelPending();
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
        m_frameSize = QSize();
        return;
    }
    const double pixAspect = aspectOf(m_preview.size());
    const double aspect = m_frameAspect > 0.0 ? m_frameAspect : pixAspect;
    int w = availW;
    int h = qRound(static_cast<double>(availW) / aspect);
    if (h > availH)
    {
        h = availH;
        w = qRound(static_cast<double>(availH) * aspect);
    }
    w = std::clamp(w, 1, availW);
    h = std::clamp(h, 1, availH);
    m_frameSize = QSize(w, h);

    // P5: rasterize at device resolution so HiDPI panels stay sharp.
    const qreal dpr = effectiveDevicePixelRatio();
    const QSize device(std::max(1, qRound(w * dpr)), std::max(1, qRound(h * dpr)));
    const double mismatch = pixAspect > aspect ? pixAspect / aspect : aspect / pixAspect;
    QPixmap out;
    if (mismatch <= kMaxCropAspectRatio)
    {
        // Center-crop the stage into the stable frame.
        out = m_preview.scaled(device, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);
        if (out.size() != device)
            out = out.copy((out.width() - device.width()) / 2, (out.height() - device.height()) / 2,
                           device.width(), device.height());
    }
    else
    {
        // Wildly different placeholder: fit it inside the frame instead.
        const QPixmap fitted =
            m_preview.scaled(device, Qt::KeepAspectRatio, Qt::SmoothTransformation);
        out = QPixmap(device);
        out.fill(Qt::transparent);
        QPainter p(&out);
        p.drawPixmap((device.width() - fitted.width()) / 2, (device.height() - fitted.height()) / 2,
                     fitted);
    }
    out.setDevicePixelRatio(dpr);
    m_scaled = out;
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
    const QColor secondary =
        QColor(mviewer::ui::Theme::themeColor(mviewer::ui::ThemeRole::TextSecondary));
    painter.fillRect(rect(), base);

    if (!m_hasImage)
    {
        painter.setPen(secondary);
        QFont f = font();
        f.setPointSize(9);
        painter.setFont(f);
        painter.drawText(rect(), Qt::AlignCenter,
                         tr("Drop images or folders here\nor press Ctrl+O to open a folder"));
        return;
    }

    const int pad = 10;
    const int txtH = 56;
    const QRect imgArea(pad, pad, width() - pad * 2, height() - pad * 2 - txtH);
    const QSize frame = displayedFrameSize();
    if (!m_scaled.isNull() && validSize(frame))
    {
        const int x = imgArea.x() + (imgArea.width() - frame.width()) / 2;
        const int y = imgArea.y() + (imgArea.height() - frame.height()) / 2;
        const QRect target(x, y, frame.width(), frame.height());
        painter.fillRect(target, well);
        painter.setPen(pal.color(QPalette::Mid));
        painter.drawRect(x - 1, y - 1, frame.width() + 1, frame.height() + 1);
        const bool fading = transitionRunning() && !m_fadeFrom.isNull();
        painter.setRenderHint(QPainter::SmoothPixmapTransform, fading || frame != m_frameSize);
        if (fading)
        {
            painter.drawPixmap(target, m_fadeFrom);
            painter.setOpacity(std::clamp(m_transitionT, 0.0, 1.0));
        }
        painter.drawPixmap(target, m_scaled);
        painter.setOpacity(1.0);
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
                     tr("Sample luminance %1   RGB(%2,%3,%4)")
                         .arg(m_lumMean, 0, 'f', 1)
                         .arg(m_rMean)
                         .arg(m_gMean)
                         .arg(m_bMean));
}
