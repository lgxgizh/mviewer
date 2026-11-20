#include "metadataoverlay.h"
#include "widgets/histogramwidget.h"

#include "Theme.h"
#include "ThemeMetrics.h"

#include "core/image/MetadataReader.h"
#include "core/image/RawMetadata.h"

#include <QApplication>
#include <QClipboard>
#include <QDateTime>
#include <QFileInfo>
#include <QFontMetrics>
#include <QHideEvent>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPointer>
#include <QResizeEvent>
#include <QVector>

#include <algorithm>
#include <cmath>
#include <cstdint>

MetadataOverlay::MetadataOverlay(QWidget *parent) : QWidget(parent)
{
    m_consumerId = "metadata-overlay-" + std::to_string(reinterpret_cast<std::uintptr_t>(this));
    setVisible(false);
    setAttribute(Qt::WA_TransparentForMouseEvents, false);
    setFocusPolicy(Qt::StrongFocus);
    setMouseTracking(true);
    setAutoFillBackground(false);
    setAttribute(Qt::WA_TranslucentBackground, true);

    // P0: Embedded mini histogram (rendered as overlay child widget). Its
    // height comes from layout(): a short viewer shrinks it before the metadata
    // is pushed off the window, so the widget must not pin a fixed height.
    m_histogram = new HistogramWidget(this);
    m_histogram->setMinimumHeight(kMinHistogramHeight);
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

namespace
{
QString formatFileSize(qint64 bytes)
{
    constexpr qint64 kKiB = 1024;
    constexpr qint64 kMiB = 1024 * kKiB;
    constexpr qint64 kGiB = 1024 * kMiB;
    if (bytes < kKiB)
        return QString("%1 B").arg(bytes);
    const double bytesD = static_cast<double>(bytes);
    if (bytes < kMiB)
        return QString("%1 KB").arg(bytesD / 1024.0, 0, 'f', 1);
    if (bytes < kGiB)
        return QString("%1 MB").arg(bytesD / (1024.0 * 1024.0), 0, 'f', 2);
    return QString("%1 GB").arg(bytesD / (1024.0 * 1024.0 * 1024.0), 0, 'f', 2);
}

QString lookup(const std::map<std::string, std::string> &m, const char *key)
{
    auto it = m.find(key);
    return it != m.end() ? QString::fromStdString(it->second) : QString();
}
} // namespace

// Greedy word wrap. A single word wider than the box (a long path, a colour
// space name) is hard-broken so it can never push past the panel edge.
QStringList MetadataOverlay::wrapLine(const QFontMetrics &metrics, const QString &text, int width)
{
    if (width <= 0 || metrics.horizontalAdvance(text) <= width)
        return {text};

    QStringList rows;
    QString current;
    const QStringList words = text.split(QLatin1Char(' '), Qt::SkipEmptyParts);
    for (const QString &word : words)
    {
        if (metrics.horizontalAdvance(word) > width)
        {
            if (!current.isEmpty())
            {
                rows << current;
                current.clear();
            }
            QString chunk = word;
            while (metrics.horizontalAdvance(chunk) > width && chunk.size() > 1)
            {
                int cut = chunk.size() - 1;
                while (cut > 1 && metrics.horizontalAdvance(chunk.left(cut)) > width)
                    --cut;
                rows << chunk.left(cut);
                chunk = chunk.mid(cut);
            }
            current = chunk;
            continue;
        }
        const QString candidate = current.isEmpty() ? word : current + QLatin1Char(' ') + word;
        if (metrics.horizontalAdvance(candidate) > width)
        {
            rows << current;
            current = word;
        }
        else
        {
            current = candidate;
        }
    }
    if (!current.isEmpty())
        rows << current;
    return rows;
}

MetadataOverlay::Layout MetadataOverlay::layout(const QSize &viewerSize) const
{
    Layout out;
    const int margin = kInfoMargin;
    const int padding = kInfoPadding;
    const int availableW = std::max(120, viewerSize.width() - margin * 2);
    const int availableH = std::max(120, viewerSize.height() - margin * 2);

    QFont bodyFont = font();
    bodyFont.setPixelSize(kFontSize);
    const QFontMetrics bodyMetrics(bodyFont);
    QFont titleFont = font();
    titleFont.setPixelSize(kTitleFontSize);
    titleFont.setBold(true);
    const QFontMetrics titleMetrics(titleFont);

    const int lineH = bodyMetrics.height() + 4;
    const int titleH = m_shortName.isEmpty() ? 0 : titleMetrics.height() + 2;

    // The panel grows to its widest value, capped at a readable measure and at
    // the viewer itself; anything longer wraps instead of being clipped.
    int contentW = titleMetrics.horizontalAdvance(m_shortName);
    for (const QString &line : m_lines)
        contentW = std::max(contentW, bodyMetrics.horizontalAdvance(line));
    const int maxTextW = std::min(kMaxInfoWidth, availableW) - padding * 2;
    const int minTextW = kMinInfoWidth - padding * 2;
    int textW = std::clamp(contentW, minTextW, std::max(1, maxTextW));

    // Fit the panel to the wrapped result: wrapping at the widest measure can
    // leave the box much wider than the text it actually holds. A narrower
    // measure only re-wraps, never widens a row, so this converges.
    QStringList wrapped;
    for (int pass = 0; pass < 4; ++pass)
    {
        wrapped.clear();
        for (const QString &line : m_lines)
            wrapped += wrapLine(bodyMetrics, line, textW);
        int usedW = titleMetrics.horizontalAdvance(m_shortName);
        for (const QString &row : wrapped)
            usedW = std::max(usedW, bodyMetrics.horizontalAdvance(row));
        const int fitted = std::clamp(usedW, minTextW, std::max(1, maxTextW));
        if (fitted == textW)
            break;
        textW = fitted;
    }
    const int boxW = textW + padding * 2;

    const bool hasHist = m_histogram && m_histogram->isVisible();
    int histH = hasHist ? kHistogramHeight : 0;
    int boxH = padding + titleH + (titleH > 0 ? kTitleGap : 0) +
               static_cast<int>(wrapped.size()) * lineH + padding + kHintGap + kHintHeight;
    if (hasHist)
        boxH += kHistogramGap + histH;

    // A viewer too short for text + histogram: shrink the histogram first so
    // the metadata itself is never pushed off the window.
    if (boxH > availableH && hasHist)
    {
        const int withoutHist = boxH - (kHistogramGap + histH);
        const int room = availableH - withoutHist - kHistogramGap;
        histH = room >= kMinHistogramHeight ? std::min(histH, room) : 0;
        boxH = withoutHist + (histH > 0 ? kHistogramGap + histH : 0);
    }

    const int x = std::max(margin, viewerSize.width() - boxW - margin);
    int y = margin;
    if (y + boxH > viewerSize.height() - margin)
        y = std::max(margin, viewerSize.height() - boxH - margin);

    out.box = QRect(x, y, boxW, boxH);
    int cursor = y + padding;
    if (titleH > 0)
    {
        out.title = QRect(x + padding, cursor, textW, titleH);
        out.titleText = titleMetrics.elidedText(m_shortName, Qt::ElideMiddle, textW);
        cursor += titleH + kTitleGap;
    }
    out.lines.clear();
    out.lines.reserve(wrapped.size());
    out.texts = wrapped;
    for (int i = 0; i < wrapped.size(); ++i)
    {
        out.lines.append(QRect(x + padding, cursor, textW, lineH));
        cursor += lineH;
    }
    if (histH > 0)
    {
        cursor += kHistogramGap;
        out.histogram = QRect(x + padding, cursor, textW, histH);
        cursor += histH;
    }
    cursor += kHintGap;
    out.hint = QRect(x + padding, cursor, textW, kHintHeight);
    return out;
}

void MetadataOverlay::requestMetadata()
{
    const QString path = m_requestedPath;
    const uint64_t generation = m_requestGeneration;
    QPointer<MetadataOverlay> guard(this);
    m_requestActive = true;
    mviewer::core::MetadataPresentationService::instance().request(
        path.toStdString(), m_consumerId,
        [guard, path,
         generation](const mviewer::core::MetadataPresentationService::Snapshot &snapshot)
        {
            // The callback may run on a worker thread, so it must not touch
            // the QPointer there.
            if (!qApp)
                return;
            QMetaObject::invokeMethod(
                qApp,
                [guard, path, generation, snapshot]()
                {
                    if (!guard || !guard->isVisible() || guard->m_requestedPath != path ||
                        guard->m_requestGeneration != generation)
                        return;
                    guard->m_requestActive = false;
                    if (!snapshot.valid())
                    {
                        // The service delivers an empty snapshot for a deleted,
                        // corrupt or unsupported file; painting it showed
                        // "Size: 0 B / Format: " instead of clearing the overlay (its
                        // two sibling consumers already guard for this).
                        guard->m_lines.clear();
                        guard->update();
                        return;
                    }
                    guard->buildContent(snapshot);
                    guard->update();
                },
                Qt::QueuedConnection);
        });
}

void MetadataOverlay::appendFileInfo(const mviewer::domain::ImageMetadata &meta)
{
    if (!m_shortName.isEmpty())
        m_lines << tr("File: %1").arg(m_shortName);

    QString pathStr =
        QString::fromUtf8(meta.filePath.data(), static_cast<int>(meta.filePath.size()));
    if (pathStr.isEmpty())
        pathStr = m_requestedPath;
    if (!pathStr.isEmpty())
        m_lines << tr("Path: %1").arg(pathStr);

    if (meta.fileSize > 0)
        m_lines << tr("Size: %1").arg(formatFileSize(meta.fileSize));

    QString fmt = QString::fromStdString(meta.format);
    if (fmt.isEmpty() && !pathStr.isEmpty())
    {
        const QString ext = QFileInfo(pathStr).suffix().toUpper();
        fmt = (ext == "JPG") ? "JPEG" : ext;
    }
    if (!fmt.isEmpty())
        m_lines << tr("Format: %1").arg(fmt);

    if (meta.width > 0 && meta.height > 0)
        m_lines << tr("Resolution: %1 × %2").arg(meta.width).arg(meta.height);

    if (meta.channels > 0)
        m_lines << tr("Channels: %1 · Depth: %2-bit").arg(meta.channels).arg(meta.bitDepth);

    if (!meta.colorSpace.empty())
        m_lines << tr("Color space: %1").arg(QString::fromStdString(meta.colorSpace));

    if (meta.dpiX > 0 || meta.dpiY > 0)
        m_lines << tr("DPI: %1 × %2").arg(meta.dpiX).arg(meta.dpiY);
}

void MetadataOverlay::appendExifInfo(const mviewer::domain::ImageMetadata &meta)
{
    if (meta.textKeys.empty())
        return;

    const auto make = lookup(meta.textKeys, "Make");
    const auto model = lookup(meta.textKeys, "Model");
    if (!make.isEmpty() || !model.isEmpty())
        m_lines << tr("Camera: %1 %2").arg(make, model).trimmed();

    const auto dateTime = lookup(meta.textKeys, "DateTimeOriginal");
    if (!dateTime.isEmpty())
        m_lines << tr("Taken: %1").arg(dateTime);

    const auto iso = lookup(meta.textKeys, "ISOSpeedRatings");
    if (!iso.isEmpty())
        m_lines << QString("ISO: %1").arg(iso);

    const auto exp = lookup(meta.textKeys, "ExposureTime");
    if (!exp.isEmpty())
        m_lines << tr("Shutter: %1s").arg(exp);

    const auto fnum = lookup(meta.textKeys, "FNumber");
    if (!fnum.isEmpty())
        m_lines << tr("Aperture: f/%1").arg(fnum);

    const auto fl = lookup(meta.textKeys, "FocalLength");
    if (!fl.isEmpty())
        m_lines << tr("Focal length: %1mm").arg(fl);

    const auto lensModel = lookup(meta.textKeys, "LensModel");
    const auto lensMake = lookup(meta.textKeys, "LensMake");
    if (!lensModel.isEmpty() || !lensMake.isEmpty())
        m_lines << tr("Lens: %1 %2").arg(lensMake, lensModel).trimmed();

    const auto sw = lookup(meta.textKeys, "Software");
    if (!sw.isEmpty())
        m_lines << tr("Software: %1").arg(sw);
}

void MetadataOverlay::appendRawInfo(const mviewer::core::RawMetadata &raw)
{
    if (!raw.make.empty() || !raw.model.empty())
    {
        const bool hasCameraLine =
            std::any_of(m_lines.cbegin(), m_lines.cend(),
                        [](const QString &l) { return l.startsWith(QStringLiteral("Camera:")); });
        if (!hasCameraLine)
            m_lines << tr("Camera: %1 %2")
                           .arg(QString::fromStdString(raw.make), QString::fromStdString(raw.model))
                           .trimmed();
    }
    if (!raw.lens.empty() || !raw.lensMaker.empty())
    {
        const bool hasLensLine = std::any_of(m_lines.cbegin(), m_lines.cend(), [](const QString &l)
                                             { return l.startsWith(QStringLiteral("Lens:")); });
        if (!hasLensLine)
            m_lines << tr("Lens: %1 %2")
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

void MetadataOverlay::appendLocationAndTimes(const mviewer::domain::ImageMetadata &meta)
{
    if (meta.hasIccProfile)
        m_lines << tr("ICC profile: embedded (%1)")
                       .arg(meta.colorSpace.empty() ? QStringLiteral("embedded")
                                                    : QString::fromStdString(meta.colorSpace));
    else if (!meta.colorSpace.empty())
        m_lines << tr("Color profile: %1").arg(QString::fromStdString(meta.colorSpace));

    if (meta.hasGps && std::isfinite(meta.gpsLatitude) && std::isfinite(meta.gpsLongitude) &&
        std::abs(meta.gpsLatitude) <= 90.0 && std::abs(meta.gpsLongitude) <= 180.0)
    {
        auto toDms = [](double dd, char pos, char neg)
        {
            const bool isNeg = (dd < 0);
            const double d = std::abs(dd);
            const int deg = static_cast<int>(d);
            const double m = (d - deg) * 60.0;
            const int min = static_cast<int>(m);
            const double s = std::clamp((m - min) * 60.0, 0.0, 59.9);
            return QString("%1°%2'%3\"%4")
                .arg(deg)
                .arg(min, 2, 10, QChar('0'))
                .arg(s, 2, 'f', 1)
                .arg(isNeg ? neg : pos);
        };
        m_lines << tr("GPS: %1  %2")
                       .arg(toDms(meta.gpsLatitude, 'N', 'S'), toDms(meta.gpsLongitude, 'E', 'W'));
        if (meta.gpsAltitude != 0.0 && std::isfinite(meta.gpsAltitude))
            m_lines << tr("Altitude: %1 m").arg(meta.gpsAltitude, 0, 'f', 1);
    }

    if (meta.modifiedEpochSec > 0)
    {
        const auto dt = QDateTime::fromSecsSinceEpoch(static_cast<qint64>(meta.modifiedEpochSec));
        m_lines << tr("Modified: %1").arg(dt.toString("yyyy-MM-dd hh:mm:ss"));
    }
}

void MetadataOverlay::buildContent(
    const mviewer::core::MetadataPresentationService::Snapshot &snapshot)
{
    m_lines.clear();

    const auto &meta = snapshot.metadata;
    m_shortName = QString::fromUtf8(meta.fileName.data(), static_cast<int>(meta.fileName.size()));
    if (m_shortName.isEmpty() && !meta.filePath.empty())
    {
        m_shortName = QFileInfo(QString::fromUtf8(meta.filePath.data(),
                                                  static_cast<int>(meta.filePath.size())))
                          .fileName();
    }
    if (m_shortName.isEmpty() && !m_requestedPath.isEmpty())
        m_shortName = QFileInfo(m_requestedPath).fileName();

    appendFileInfo(meta);
    appendExifInfo(meta);
    appendRawInfo(snapshot.raw);
    appendLocationAndTimes(meta);
}

void MetadataOverlay::paintEvent(QPaintEvent *)
{
    if (m_lines.isEmpty() && m_shortName.isEmpty())
        return;

    const Layout lay = layout(QSize(width(), height()));

    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    QColor fill(mviewer::ui::Theme::themeColor(mviewer::ui::ThemeRole::Bg1));
    fill.setAlpha(kInfoAlpha);
    QColor edge(mviewer::ui::Theme::themeColor(mviewer::ui::ThemeRole::Border));
    QColor ink(mviewer::ui::Theme::themeColor(mviewer::ui::ThemeRole::TextPrimary));
    QColor muted(mviewer::ui::Theme::themeColor(mviewer::ui::ThemeRole::TextSecondary));

    QPainterPath bgPath;
    bgPath.addRoundedRect(QRectF(lay.box), mviewer::ui::metrics::kRadiusPanel,
                          mviewer::ui::metrics::kRadiusPanel);
    p.fillPath(bgPath, fill);

    p.setPen(QPen(edge, 1));
    p.drawPath(bgPath);

    p.setPen(ink);
    if (!lay.title.isEmpty())
    {
        QFont hf = font();
        hf.setPixelSize(kTitleFontSize);
        hf.setBold(true);
        p.setFont(hf);
        p.drawText(lay.title, Qt::AlignLeft | Qt::AlignVCenter, lay.titleText);
    }

    QFont bf = font();
    bf.setPixelSize(kFontSize);
    p.setFont(bf);
    p.setPen(ink);
    for (int i = 0; i < lay.texts.size() && i < lay.lines.size(); ++i)
        p.drawText(lay.lines.at(i), Qt::AlignLeft | Qt::AlignVCenter, lay.texts.at(i));

    // P0: the histogram child is placed from the same layout as the text, so
    // the hint line never lands on top of it.
    if (m_histogram)
    {
        if (lay.histogram.isEmpty())
            m_histogram->hide();
        else
        {
            m_histogram->show();
            m_histogram->setGeometry(lay.histogram);
        }
    }

    p.setPen(muted);
    QFont sf = font();
    sf.setPixelSize(10);
    p.setFont(sf);
    p.drawText(lay.hint, Qt::AlignRight | Qt::AlignVCenter, tr("Press I / ESC to close"));
}

void MetadataOverlay::mousePressEvent(QMouseEvent *event)
{
    Q_UNUSED(event);
    hide();
}

namespace
{
void hideFloatingMetadataPanel()
{
    const QWidgetList tops = QApplication::topLevelWidgets();
    for (QWidget *top : tops)
    {
        if (top->isVisible() && top->inherits("MetadataPanel"))
            top->hide();
    }
}
} // namespace

void MetadataOverlay::keyPressEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Escape || event->key() == Qt::Key_I || event->key() == Qt::Key_M)
    {
        hide();
        // The overlay and the floating panel are one toggle. Focus stays on
        // this widget, so the viewer's Esc ladder never sees the key.
        hideFloatingMetadataPanel();
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
