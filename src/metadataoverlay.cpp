#include "metadataoverlay.h"
#include "widgets/histogramwidget.h"

#include "core/image/MetadataReader.h"
#include "core/image/RawMetadata.h"

#include <QApplication>
#include <QClipboard>
#include <QDateTime>
#include <QFileInfo>
#include <QKeyEvent>
#include <QHideEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPointer>
#include <QResizeEvent>

#include <algorithm>
#include <cstdint>

MetadataOverlay::MetadataOverlay(QWidget *parent) : QWidget(parent)
{
    m_consumerId = "metadata-overlay-" + std::to_string(reinterpret_cast<std::uintptr_t>(this));
    setVisible(false);
    setAttribute(Qt::WA_TransparentForMouseEvents, false);
    setFocusPolicy(Qt::StrongFocus);
    setMouseTracking(true);
    setAutoFillBackground(false);

    // P0: Embedded mini histogram (rendered as overlay child widget).
    m_histogram = new HistogramWidget(this);
    m_histogram->setFixedHeight(kHistogramHeight);
    m_histogram->hide();
}

void MetadataOverlay::setImage(const QString &path)
{
    m_requestedPath = path;
    ++m_requestGeneration;
    m_requestActive = false;
    if (isVisible())
        requestMetadata();
}

void MetadataOverlay::showForImage(const QString &path)
{
    if (path.isEmpty())
    {
        mviewer::core::MetadataPresentationService::instance().cancel(m_consumerId);
        m_requestActive = false;
        return;
    }
    if (m_requestedPath != path)
        setImage(path);
    if (parentWidget())
        setGeometry(parentWidget()->rect());
    show();
    raise();
    setFocus();
    emit visibilityChanged(true);
    if (!m_requestActive)
        requestMetadata();
}

void MetadataOverlay::toggle()
{
    if (isVisible())
        hide();
    else if (parentWidget())
    {
        setGeometry(parentWidget()->rect());
        show();
        raise();
        setFocus();
        if (!m_requestActive)
            requestMetadata();
    }
}

void MetadataOverlay::hide()
{
    mviewer::core::MetadataPresentationService::instance().cancel(m_consumerId);
    m_requestActive = false;
    ++m_requestGeneration;
    QWidget::hide();
    m_lines.clear();
    m_shortName.clear();
    if (m_histogram)
    {
        // Drop any rendered histogram data too — re-showing the overlay must
        // not resurrect the previous image's histogram before the fresh async
        // delivery arrives.
        m_histogram->clear();
        m_histogram->hide();
    }
    emit visibilityChanged(false);
}

void MetadataOverlay::hideEvent(QHideEvent *event)
{
    // Parent/window visibility changes do not necessarily pass through the
    // public hide() helper. Invalidate the consumer in that path too, so a
    // hidden overlay never keeps metadata presentation work alive.
    mviewer::core::MetadataPresentationService::instance().cancel(m_consumerId);
    ++m_requestGeneration;
    m_requestActive = false;
    QWidget::hideEvent(event);
}

void MetadataOverlay::setHistogram(const mviewer::core::Histogram &hist)
{
    if (!m_histogram)
        return;
    if (hist.r.empty())
    {
        m_histogram->clear();
        m_histogram->hide();
        return;
    }
    m_histogram->setHistograms({hist});
    m_histogram->show();
    if (isVisible())
        update(); // trigger paintEvent to reposition
}

void MetadataOverlay::positionHistogram(const QRect &boxRect)
{
    if (!m_histogram || !m_histogram->isVisible())
        return;
    const int padding = 12;
    const int lineH = fontMetrics().height() + 4;
    const int bodyEnd = (m_lines.size() + 1) * lineH + padding;
    const int x = boxRect.x() + padding;
    const int y = boxRect.y() + bodyEnd + 4;
    const int w = boxRect.width() - padding * 2;
    m_histogram->setGeometry(x, y, w, kHistogramHeight);
}

namespace
{
QString formatFileSize(qint64 bytes)
{
    if (bytes < 1024)
        return QString("%1 B").arg(bytes);
    if (bytes < 1024 * 1024)
        return QString("%1 KB").arg(bytes / 1024.0, 0, 'f', 1);
    if (bytes < 1024LL * 1024 * 1024)
        return QString("%1 MB").arg(bytes / (1024.0 * 1024), 0, 'f', 2);
    return QString("%1 GB").arg(bytes / (1024.0 * 1024 * 1024), 0, 'f', 2);
}

QString lookup(const std::map<std::string, std::string> &m, const char *key)
{
    auto it = m.find(key);
    return it != m.end() ? QString::fromStdString(it->second) : QString();
}
} // namespace

void MetadataOverlay::requestMetadata()
{
    const QString path = m_requestedPath;
    const uint64_t generation = m_requestGeneration;
    QPointer<MetadataOverlay> guard(this);
    m_requestActive = true;
    mviewer::core::MetadataPresentationService::instance().request(
        path.toStdString(), m_consumerId,
        [guard, path, generation](const mviewer::core::MetadataPresentationService::Snapshot &snapshot)
        {
            if (!guard || !guard->isVisible() || guard->m_requestedPath != path ||
                guard->m_requestGeneration != generation)
                return;
            guard->m_requestActive = false;
            guard->buildContent(snapshot);
            guard->update();
        });
}

void MetadataOverlay::buildContent(
    const mviewer::core::MetadataPresentationService::Snapshot &snapshot)
{
    m_lines.clear();

    const auto &meta = snapshot.metadata;

    m_shortName = QString::fromUtf8(meta.fileName.data(), static_cast<int>(meta.fileName.size()));

    // Basic file info
    m_lines << QString("文件: %1").arg(m_shortName);
    m_lines << QString("路径: %1")
                   .arg(QString::fromUtf8(meta.filePath.data(), static_cast<int>(meta.filePath.size())));
    m_lines << QString("尺寸: %1").arg(formatFileSize(meta.fileSize));
    m_lines << QString("格式: %1").arg(QString::fromStdString(meta.format));

    // Image dimensions
    if (meta.width > 0 && meta.height > 0)
        m_lines << QString("分辨率: %1 × %2").arg(meta.width).arg(meta.height);

    if (meta.channels > 0)
        m_lines << QString("通道: %1 · 色深: %2-bit").arg(meta.channels).arg(meta.bitDepth);

    if (!meta.colorSpace.empty())
        m_lines << QString("色彩空间: %1").arg(QString::fromStdString(meta.colorSpace));

    // DPI
    if (meta.dpiX > 0 || meta.dpiY > 0)
        m_lines << QString("DPI: %1 × %2").arg(meta.dpiX).arg(meta.dpiY);

    // EXIF text keys (from embedded metadata)
    if (!meta.textKeys.empty())
    {
        const auto make = lookup(meta.textKeys, "Make");
        const auto model = lookup(meta.textKeys, "Model");
        if (!make.isEmpty() || !model.isEmpty())
            m_lines << QString("相机: %1 %2").arg(make, model).trimmed();

        const auto dateTime = lookup(meta.textKeys, "DateTimeOriginal");
        if (!dateTime.isEmpty())
            m_lines << QString("拍摄: %1").arg(dateTime);

        const auto iso = lookup(meta.textKeys, "ISOSpeedRatings");
        if (!iso.isEmpty())
            m_lines << QString("ISO: %1").arg(iso);

        const auto exp = lookup(meta.textKeys, "ExposureTime");
        if (!exp.isEmpty())
            m_lines << QString("快门: %1s").arg(exp);

        const auto fnum = lookup(meta.textKeys, "FNumber");
        if (!fnum.isEmpty())
            m_lines << QString("光圈: f/%1").arg(fnum);

        const auto fl = lookup(meta.textKeys, "FocalLength");
        if (!fl.isEmpty())
            m_lines << QString("焦距: %1mm").arg(fl);

        // Lens (EXIF LensModel / LensMake, with RAW fallback below).
        const auto lensModel = lookup(meta.textKeys, "LensModel");
        const auto lensMake = lookup(meta.textKeys, "LensMake");
        if (!lensModel.isEmpty() || !lensMake.isEmpty())
            m_lines << QString("镜头: %1 %2").arg(lensMake, lensModel).trimmed();

        const auto sw = lookup(meta.textKeys, "Software");
        if (!sw.isEmpty())
            m_lines << QString("软件: %1").arg(sw);
    }

    // RAW sidecar EXIF (camera / lens) when the generic textKeys path is empty.
    {
        const auto &raw = snapshot.raw;
        if (!raw.make.empty() || !raw.model.empty())
        {
            const bool hasCameraLine =
                std::any_of(m_lines.cbegin(), m_lines.cend(),
                            [](const QString &l) { return l.startsWith(QStringLiteral("相机:")); });
            if (!hasCameraLine)
                m_lines << QString("相机: %1 %2")
                               .arg(QString::fromStdString(raw.make),
                                    QString::fromStdString(raw.model))
                               .trimmed();
        }
        if (!raw.lens.empty() || !raw.lensMaker.empty())
        {
            const bool hasLensLine =
                std::any_of(m_lines.cbegin(), m_lines.cend(),
                            [](const QString &l) { return l.startsWith(QStringLiteral("镜头:")); });
            if (!hasLensLine)
                m_lines << QString("镜头: %1 %2")
                               .arg(QString::fromStdString(raw.lensMaker),
                                    QString::fromStdString(raw.lens))
                               .trimmed();
        }
        if (raw.iso > 0)
        {
            const bool hasIso = std::any_of(m_lines.cbegin(), m_lines.cend(), [](const QString &l)
                                            { return l.startsWith(QStringLiteral("ISO:")); });
            if (!hasIso)
                m_lines << QString("ISO: %1").arg(raw.iso);
        }
    }

    // ICC profile
    if (meta.hasIccProfile)
        m_lines << QString("ICC 配置: 已嵌入 (%1)")
                       .arg(meta.colorSpace.empty() ? QStringLiteral("embedded")
                                                    : QString::fromStdString(meta.colorSpace));
    else if (!meta.colorSpace.empty())
        m_lines << QString("色彩配置: %1").arg(QString::fromStdString(meta.colorSpace));

    // P0: GPS coordinates (decimal degrees → DMS for readability).
    if (meta.hasGps)
    {
        auto toDms = [](double dd, char pos, char neg)
        {
            const bool isNeg = (dd < 0);
            double d = std::abs(dd);
            int deg = static_cast<int>(d);
            double m = (d - deg) * 60.0;
            int min = static_cast<int>(m);
            double s = (m - min) * 60.0;
            return QString("%1°%2'%3\"%4")
                .arg(deg)
                .arg(min, 2, 10, QChar('0'))
                .arg(s, 2, 'f', 1)
                .arg(isNeg ? neg : pos);
        };
        m_lines << QString("GPS: %1  %2")
                       .arg(toDms(meta.gpsLatitude, 'N', 'S'), toDms(meta.gpsLongitude, 'E', 'W'));
        if (meta.gpsAltitude != 0.0)
            m_lines << QString("海拔: %1 m").arg(meta.gpsAltitude, 0, 'f', 1);
    }

    // Modified time
    if (meta.modifiedEpochSec > 0)
    {
        const auto dt = QDateTime::fromSecsSinceEpoch(static_cast<qint64>(meta.modifiedEpochSec));
        m_lines << QString("修改: %1").arg(dt.toString("yyyy-MM-dd hh:mm:ss"));
    }
    // P0: Histogram is rendered as a child widget (m_histogram), not in text lines.
}

void MetadataOverlay::paintEvent(QPaintEvent *)
{
    if (m_lines.isEmpty())
        return;

    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    const int lineH = fontMetrics().height() + 4;
    const int padding = 12;
    const int boxW = kInfoRectWidth;
    // P0: Reserve space for embedded histogram if it has data.
    const int histExtra =
        (m_histogram && m_histogram->isVisible()) ? kHistogramHeight + padding : 0;
    const int boxH = (m_lines.size() + 1) * lineH + padding * 2 + histExtra;

    const int x = width() - boxW - 20;
    const int y = 20;
    QRect boxRect(x, y, boxW, boxH);

    // Semi-transparent dark background
    QPainterPath bgPath;
    bgPath.addRoundedRect(QRectF(boxRect), 8.0, 8.0);
    p.fillPath(bgPath, QColor(20, 20, 20, 200));

    // Border
    p.setPen(QPen(QColor(255, 255, 255, 60), 1));
    p.drawPath(bgPath);

    // Header
    p.setPen(QColor(255, 255, 255, 255));
    QFont hf = font();
    hf.setPixelSize(kFontSize + 2);
    hf.setBold(true);
    p.setFont(hf);
    p.drawText(QRect(x + padding, y + padding, boxW - padding * 2, lineH),
               Qt::AlignLeft | Qt::AlignVCenter, m_shortName);

    // Body lines
    QFont bf = font();
    bf.setPixelSize(kFontSize);
    p.setFont(bf);
    p.setPen(QColor(220, 220, 220, 255));

    for (int i = 0; i < m_lines.size(); ++i)
    {
        p.drawText(QRect(x + padding, y + padding + (i + 1) * lineH, boxW - padding * 2, lineH),
                   Qt::AlignLeft | Qt::AlignVCenter, m_lines.at(i));
    }

    // P0: Position histogram widget at the bottom of the info box.
    if (m_histogram && m_histogram->isVisible())
        positionHistogram(boxRect);

    // Hint
    p.setPen(QColor(150, 150, 150, 255));
    QFont sf = font();
    sf.setPixelSize(10);
    p.setFont(sf);
    p.drawText(QRect(x + padding, y + boxH - 16, boxW - padding * 2, 16),
               Qt::AlignRight | Qt::AlignVCenter, "按 I / ESC 关闭");
}

void MetadataOverlay::mousePressEvent(QMouseEvent *event)
{
    Q_UNUSED(event);
    hide();
}

void MetadataOverlay::keyPressEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Escape || event->key() == Qt::Key_I || event->key() == Qt::Key_M)
    {
        hide();
        event->accept();
        return;
    }
    // Ctrl+C copies the full metadata block to the clipboard — the overlay is a
    // paint-only widget (no selectable text), so this is the copy affordance.
    if (event->matches(QKeySequence::Copy))
    {
        QApplication::clipboard()->setText(m_lines.join('\n'));
        event->accept();
        return;
    }
    QWidget::keyPressEvent(event);
}

void MetadataOverlay::resizeEvent(QResizeEvent *)
{
    if (parentWidget() && isVisible())
        setGeometry(parentWidget()->rect());
}
