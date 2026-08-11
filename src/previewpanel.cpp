#include "previewpanel.h"

#include "core/cache/CacheManager.h"
#include "core/image/Decoder.h"
#include "core/image/ImageRepository.h"
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

PreviewPanel::PreviewPanel(QWidget *parent) : QWidget(parent)
{
    setMinimumSize(220, 220);
}

PreviewPanel::~PreviewPanel()
{
    cancelPending();
}

std::string PreviewPanel::previewCacheKey(const std::string &path)
{
    return ImageRepository::instance().makeKey(path) + "#preview" + std::to_string(kPreviewMaxEdge);
}

void PreviewPanel::setImage(const QString &path)
{
    m_path = path;
    cancelPending();
    ++m_requestGen;
    m_preview = QPixmap();
    m_scaled = QPixmap();
    m_hasImage = false;
    m_previewW = 0;
    m_previewH = 0;
    m_imgW = 0;
    m_imgH = 0;
    m_fileSize = 0;
    m_lumMean = 0.0;
    m_rMean = m_gMean = m_bMean = 0;
    if (path.isEmpty())
    {
        // Clear synchronously when a folder changes. This prevents an old
        // decoded frame from remaining visible while the next directory is
        // still being scanned asynchronously.
        update();
        return;
    }
    update();

    // A SINGLE scaled decode on the Thumbnail pool (never DecodePool, never
    // ImageRepository::loadAsync): the preview duplicates nothing and the UI
    // thread only ever materializes a <= kPreviewMaxEdge QPixmap. The scaled
    // result is cached in the existing CacheLevel::Preview layer. The worker
    // also computes the preview stats (sample means over the scaled buffer),
    // reads the original dimensions/orientation via QImageReader and the file
    // size, checks TaskContext cancellation before/after the work, and marshals
    // an owned QImage + stats + metadata to the UI thread through qApp.
    //
    // M27 lifetime closure: the worker callback captures a QPointer (never a
    // raw `this`) and re-checks it on the UI thread through qApp (which
    // outlives every panel); the queued lambda re-checks the guard AND the
    // request generation, so a panel destroyed mid-decode or superseded by a
    // newer setImage() (even A -> B -> A) can never be touched by an old
    // delivery. The destructor additionally soft-cancels the task so queued
    // stale work exits before decoding.
    const uint64_t gen = m_requestGen;
    QPointer<PreviewPanel> guard(this);
    const std::string stdPath = path.toStdString();

    auto handle = TaskScheduler::instance().submit(
        TaskScheduler::Priority::Thumbnail,
        [stdPath, path, gen, guard](const TaskScheduler::TaskContext &ctx)
        {
            if (ctx.isCancelled())
                return; // queued stale task — stop before any work
            const std::string cacheKey = previewCacheKey(stdPath);
            mviewer::core::PreviewStats stats{};
            QImage qimg;
            int srcW = 0;
            int srcH = 0;
            qint64 fileSize = 0;
            ImageData img;
            if (CacheManager::instance().getMemory(CacheLevel::Preview, cacheKey, img))
            {
                // Warm scaled preview — reuse it, still recompute the stats on
                // the worker from the cached buffer.
            }
            else
            {
                img = Decoder::decodeScaled(stdPath, kPreviewMaxEdge);
                if (!img.isNull())
                    CacheManager::instance().putMemory(CacheLevel::Preview, cacheKey, img);
            }
            if (!img.isNull())
            {
                stats = mviewer::core::computePreviewStats(img);
                // Original dimensions/orientation come from the source header,
                // not the scaled buffer, so the panel shows the real size.
                // QImageReader::size() is the ENCODED (raw) size even after
                // setAutoTransform(true); the 90-degree EXIF rotations swap the
                // axes, so apply reader.transformation() before reporting.
                QImageReader reader(QString::fromStdString(stdPath));
                reader.setAutoTransform(true);
                QSize src = reader.size();
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
                    srcW = src.width();
                    srcH = src.height();
                }
                else
                {
                    // Header probing failed (the decoder still produced a
                    // buffer, e.g. via a fallback path): report the decoded
                    // scaled dimensions rather than a 0x0 preview size.
                    srcW = img.width;
                    srcH = img.height;
                }
            }
            fileSize = QFileInfo(QString::fromStdString(stdPath)).size();
            if (ctx.isCancelled())
                return; // superseded while decoding — drop before delivery
            if (!img.isNull())
                qimg = mvcore::toQImage(img);
            QMetaObject::invokeMethod(qApp,
                                      [path, gen, guard, qimg, stats, srcW, srcH, fileSize]()
                                      {
                                          PreviewPanel *panel = guard.data();
                                          if (!panel)
                                              return;
                                          // Stale-callback guard: discard deliveries from
                                          // superseded requests (different path OR an older
                                          // generation of the same path, i.e. A -> B -> A where the
                                          // first A completes last). Without the generation check,
                                          // the path-only guard lets an old A overwrite a newer A
                                          // of the same path.
                                          if (path != panel->m_path || gen != panel->m_requestGen)
                                              return;
                                          // Every accepted terminal delivery releases the
                                          // matching completed handle — success OR failure —
                                          // so a rejected decode never retains a finished
                                          // task until the next request or destruction.
                                          panel->resetMatchingHandle(gen);
                                          if (qimg.isNull())
                                          {
                                              panel->m_hasImage = false;
                                              panel->update();
                                              return;
                                          }
                                          panel->m_preview = QPixmap::fromImage(qimg);
                                          if (panel->m_preview.isNull())
                                          {
                                              panel->m_hasImage = false;
                                              panel->update();
                                              return;
                                          }
                                          panel->m_hasImage = true;
                                          if (stats.valid)
                                          {
                                              panel->m_lumMean = stats.lumMean;
                                              panel->m_rMean = stats.rMean;
                                              panel->m_gMean = stats.gMean;
                                              panel->m_bMean = stats.bMean;
                                          }
                                          else
                                          {
                                              panel->m_lumMean = 0.0;
                                              panel->m_rMean = panel->m_gMean = panel->m_bMean = 0;
                                          }
                                          panel->m_imgW = srcW;
                                          panel->m_imgH = srcH;
                                          panel->m_previewW = panel->m_preview.width();
                                          panel->m_previewH = panel->m_preview.height();
                                          panel->m_fileSize = fileSize;
                                          panel->rebuild();
                                          panel->update();
                                      });
        });
    if (handle)
    {
        m_task = handle;
    }
    else
    {
        // Scheduler rejected the submission (paused / saturated): terminal
        // no-image state, exactly like a failed decode.
        m_hasImage = false;
        update();
    }
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
    const QString name = QFileInfo(m_path).fileName();
    painter.drawText(txtArea, Qt::AlignTop | Qt::AlignLeft,
                     name + "\n" + QString::number(m_imgW) + "×" + QString::number(m_imgH) + "  " +
                         QString::number(m_fileSize / 1024) + " KB");
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
