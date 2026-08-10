#include "previewpanel.h"

#include "core/image/ImageRepository.h"
#include "core/image/ImageStats.h"
#include "core/image/QtConvert.h"

#include <QFileInfo>
#include <QMetaObject>
#include <QPalette>
#include <QPainter>
#include <QPointer>
#include <QResizeEvent>
#include <algorithm>

PreviewPanel::PreviewPanel(QWidget *parent) : QWidget(parent)
{
    setMinimumSize(220, 220);
}

void PreviewPanel::setImage(const QString &path)
{
    m_path = path;
    if (path.isEmpty())
    {
        // Clear synchronously when a folder changes. This prevents an old
        // decoded frame from remaining visible while the next directory is
        // still being scanned asynchronously.
        m_full = QPixmap();
        m_scaled = QPixmap();
        m_hasImage = false;
        m_imgW = 0;
        m_imgH = 0;
        m_fileSize = 0;
        m_lumMean = 0.0;
        m_rMean = m_gMean = m_bMean = 0;
        update();
        return;
    }
    m_full = QPixmap();
    m_scaled = QPixmap();
    m_hasImage = false;
    update();
    // Decode off the UI thread (ImageRepository::loadAsync -> DecodePool) so
    // the bottom-left preview never blocks browsing. M26: the full-image
    // statistics are computed on the WORKER thread from the frame's pixel
    // buffer (computePreviewStats over ImageData) — the UI thread only
    // receives the small stats struct, creates the QPixmap and repaints.
    // QPixmap creation stays on the UI thread (GUI resource).
    //
    // M27 lifetime closure: the worker callback captures a QPointer (never a
    // raw `this`), checks it before ANY use, and marshals to the UI thread
    // through qApp (which outlives every panel) instead of through `this` as
    // the invoke target. The queued lambda re-checks the guard AND the request
    // generation, so a panel destroyed mid-decode or superseded by a newer
    // setImage() (even A -> B -> A) can never be touched by an old delivery.
    const uint64_t gen = ++m_requestGen;
    QPointer<PreviewPanel> guard(this);
    ImageRepository::instance().loadAsync(
        path.toStdString(),
        [path, gen, guard](const ImageRepository::Result &res)
        {
            const mviewer::core::PreviewStats stats =
                (res.success() && res.frame)
                    ? mviewer::core::computePreviewStats(res.frame->pixels())
                    : mviewer::core::PreviewStats{};
            if (!guard)
                return; // panel destroyed mid-decode — drop the result
            QMetaObject::invokeMethod(
                qApp,
                [path, gen, guard, res, stats]()
                {
                    PreviewPanel *panel = guard.data();
                    if (!panel)
                        return;
                    // Stale-callback guard: discard deliveries from superseded
                    // requests (different path OR an older generation of the
                    // same path, i.e. A -> B -> A where the first A completes
                    // last). Without the generation check, the path-only guard
                    // lets an old A overwrite a newer A of the same path.
                    if (path != panel->m_path || gen != panel->m_requestGen)
                        return;
                    if (!res.success() || !res.frame)
                    {
                        panel->m_hasImage = false;
                        panel->update();
                        return;
                    }
                    panel->m_full = QPixmap::fromImage(
                        mvcore::toQImage(res.frame->pixels()));
                    if (panel->m_full.isNull())
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
                    panel->m_imgW = panel->m_full.width();
                    panel->m_imgH = panel->m_full.height();
                    panel->m_fileSize = QFileInfo(path).size();
                    panel->rebuild();
                    panel->update();
                });
        });
}

void PreviewPanel::rebuild()
{
    if (m_full.isNull())
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
    const double s = std::min(static_cast<double>(availW) / m_full.width(),
                              static_cast<double>(availH) / m_full.height());
    m_scaled =
        m_full.scaled(static_cast<int>(m_full.width() * s), static_cast<int>(m_full.height() * s),
                      Qt::KeepAspectRatio, Qt::SmoothTransformation);
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
    painter.setPen(secondary);
    painter.drawText(txtArea.adjusted(0, 36, 0, 0), Qt::AlignTop | Qt::AlignLeft,
                     QString("亮度 %1   RGB(%2,%3,%4)")
                         .arg(m_lumMean, 0, 'f', 1)
                         .arg(m_rMean)
                         .arg(m_gMean)
                         .arg(m_bMean));
}