#include "metadataoverlay.h"

#include "core/image/MetadataReader.h"

#include <QDateTime>
#include <QFileInfo>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QResizeEvent>

MetadataOverlay::MetadataOverlay(QWidget *parent) : QWidget(parent)
{
    setVisible(false);
    setAttribute(Qt::WA_TransparentForMouseEvents, false);
    setFocusPolicy(Qt::StrongFocus);
    setMouseTracking(true);
    setAutoFillBackground(false);
}

void MetadataOverlay::setImage(const QString &path)
{
    buildContent(path);
}

void MetadataOverlay::showForImage(const QString &path)
{
    buildContent(path);
    if (m_lines.isEmpty())
        return;
    if (parentWidget())
        setGeometry(parentWidget()->rect());
    show();
    raise();
    setFocus();
    emit visibilityChanged(true);
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
    }
}

void MetadataOverlay::hide()
{
    QWidget::hide();
    m_lines.clear();
    m_shortName.clear();
    emit visibilityChanged(false);
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

void MetadataOverlay::buildContent(const QString &path)
{
    m_lines.clear();

    const auto meta = mviewer::core::MetadataReader::read(path.toStdString());

    m_shortName = QString::fromStdString(meta.fileName);

    // Basic file info
    m_lines << QString("文件: %1").arg(m_shortName);
    m_lines << QString("路径: %1").arg(QString::fromStdString(meta.filePath));
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

        const auto sw = lookup(meta.textKeys, "Software");
        if (!sw.isEmpty())
            m_lines << QString("软件: %1").arg(sw);
    }

    // ICC profile
    if (meta.hasIccProfile)
        m_lines << QString("ICC 配置: 已嵌入");

    // Modified time
    if (meta.modifiedEpochSec > 0)
    {
        const auto dt = QDateTime::fromSecsSinceEpoch(static_cast<qint64>(meta.modifiedEpochSec));
        m_lines << QString("修改: %1").arg(dt.toString("yyyy-MM-dd hh:mm:ss"));
    }
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
    const int boxH = (m_lines.size() + 1) * lineH + padding * 2;

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
    QWidget::keyPressEvent(event);
}

void MetadataOverlay::resizeEvent(QResizeEvent *)
{
    if (parentWidget() && isVisible())
        setGeometry(parentWidget()->rect());
}
