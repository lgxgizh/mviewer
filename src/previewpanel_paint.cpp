// PreviewPanel frame geometry and painting, split out of previewpanel.cpp to
// stay under the per-file line budget.
#include "previewpanel.h"

#include "Theme.h"

#include <QPainter>
#include <QPalette>
#include <QResizeEvent>

#include <algorithm>
#include <cmath>

namespace
{
// Beyond this pixmap-vs-frame aspect mismatch a stage is fitted (letterboxed
// on the well) instead of center-cropped, so a sideways placeholder is never
// reduced to a thin slice.
constexpr double kMaxCropAspectRatio = 1.3;
constexpr int kPad = 10;
constexpr int kTextHeight = 56;

bool validSize(const QSize &s)
{
    return s.isValid() && s.width() > 0 && s.height() > 0;
}

double aspectOf(const QSize &s)
{
    return validSize(s) ? static_cast<double>(s.width()) / static_cast<double>(s.height()) : 0.0;
}
} // namespace

QSize PreviewPanel::frameForAspect(double aspect) const
{
    const int availW = width() - kPad * 2;
    const int availH = height() - kPad * 2 - kTextHeight;
    if (availW <= 0 || availH <= 0 || !(aspect > 0.0) || !std::isfinite(aspect))
        return {};
    int w = availW;
    int h = qRound(static_cast<double>(availW) / aspect);
    if (h > availH)
    {
        h = availH;
        w = qRound(static_cast<double>(availH) * aspect);
    }
    return QSize(std::clamp(w, 1, availW), std::clamp(h, 1, availH));
}

void PreviewPanel::rebuild()
{
    const bool placeholder = m_quality == PresentationQuality::Placeholder;
    if (m_preview.isNull() && !placeholder)
        return;
    const double pixAspect = aspectOf(m_preview.size());
    const double aspect = m_frameAspect > 0.0 ? m_frameAspect : pixAspect;
    m_frameSize = frameForAspect(aspect);
    if (!validSize(m_frameSize) || m_preview.isNull())
    {
        // A placeholder keeps only the (empty) well; no pixels to rasterize.
        m_scaled = QPixmap();
        return;
    }
    const int w = m_frameSize.width();
    const int h = m_frameSize.height();

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
    if (m_hasImage || m_quality == PresentationQuality::Placeholder)
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

    if (!m_hasImage && m_quality != PresentationQuality::Placeholder)
    {
        painter.setPen(secondary);
        QFont f = font();
        f.setPointSize(9);
        painter.setFont(f);
        painter.drawText(rect(), Qt::AlignCenter,
                         tr("Drop images or folders here\nor press Ctrl+O to open a folder"));
        return;
    }

    const QRect imgArea(kPad, kPad, width() - kPad * 2, height() - kPad * 2 - kTextHeight);
    const QSize frame = displayedFrameSize();
    if (validSize(frame))
    {
        // The well is drawn for every stage, so a placeholder occupies the
        // same frame the pixels will (no layout jump when they land).
        const int x = imgArea.x() + (imgArea.width() - frame.width()) / 2;
        const int y = imgArea.y() + (imgArea.height() - frame.height()) / 2;
        const QRect target(x, y, frame.width(), frame.height());
        painter.fillRect(target, well);
        painter.setPen(pal.color(QPalette::Mid));
        painter.drawRect(x - 1, y - 1, frame.width() + 1, frame.height() + 1);
        if (!m_scaled.isNull())
        {
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
    }

    const QRect txtArea(8, height() - kTextHeight - 4, width() - 16, kTextHeight);
    painter.setPen(text);
    QFont f = painter.font();
    f.setPointSize(9);
    painter.setFont(f);
    // M46: lexical fileName — paint never constructs QFileInfo.
    const QString pathStr = m_presentedPath;
    const int slash = qMax(pathStr.lastIndexOf('/'), pathStr.lastIndexOf('\\'));
    const QString name = slash >= 0 ? pathStr.mid(slash + 1) : pathStr;
    // Fixed three-line layout: name / dimensions (or "—") + size / stats, so
    // late facts fill in place instead of shifting lines.
    const QString dash = QStringLiteral("\u2014");
    QString identity = name + "\n";
    identity += m_sourceDimensionsKnown
                    ? QString::number(m_imgW) + QStringLiteral("\u00d7") + QString::number(m_imgH)
                    : dash;
    if (m_fileSizeKnown)
        identity += "  " + QString::number(m_fileSize / 1024) + " KB";
    painter.drawText(txtArea, Qt::AlignTop | Qt::AlignLeft, identity);
    // The brightness/RGB figures are sample means computed over the scaled
    // preview buffer, not the full image. Until they arrive: "—".
    painter.setPen(secondary);
    const QString stats =
        m_statsKnown ? tr("Sample luminance %1   RGB(%2,%3,%4)")
                           .arg(m_lumMean, 0, 'f', 1)
                           .arg(m_rMean)
                           .arg(m_gMean)
                           .arg(m_bMean)
                     : tr("Sample luminance %1   RGB(%2,%3,%4)").arg(dash, dash, dash, dash);
    painter.drawText(txtArea.adjusted(0, 36, 0, 0), Qt::AlignTop | Qt::AlignLeft, stats);
}
