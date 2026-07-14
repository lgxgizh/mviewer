#include "previewpanel.h"

#include <QFileInfo>
#include <QPainter>
#include <QResizeEvent>
#include <algorithm>

PreviewPanel::PreviewPanel(QWidget* parent)
    : QWidget(parent)
{
    setMinimumSize(220, 220);
}

void PreviewPanel::setImage(const QString& path)
{
    m_path = path;
    m_full.load(path);
    if (m_full.isNull())
    {
        m_hasImage = false;
        update();
        return;
    }
    m_hasImage = true;
    computeStats(m_full);
    m_imgW = m_full.width();
    m_imgH = m_full.height();
    m_fileSize = QFileInfo(path).size();
    rebuild();
    update();
}

void PreviewPanel::computeStats(const QPixmap& pm)
{
    const QImage img = pm.toImage().convertToFormat(QImage::Format_RGB32);
    if (img.isNull())
    {
        m_lumMean = 0;
        m_rMean = m_gMean = m_bMean = 0;
        return;
    }
    const int w = img.width();
    const int h = img.height();
    long long sumL = 0, sumR = 0, sumG = 0, sumB = 0;
    const long long n = static_cast<long long>(w) * h;
    for (int y = 0; y < h; ++y)
    {
        const QRgb* line = reinterpret_cast<const QRgb*>(img.scanLine(y));
        for (int x = 0; x < w; ++x)
        {
            const QRgb c = line[x];
            const int r = qRed(c), g = qGreen(c), b = qBlue(c);
            sumR += r;
            sumG += g;
            sumB += b;
            sumL += static_cast<long long>(0.299 * r + 0.587 * g + 0.114 * b);
        }
    }
    m_lumMean = static_cast<double>(sumL) / n;
    m_rMean = static_cast<int>(sumR / n);
    m_gMean = static_cast<int>(sumG / n);
    m_bMean = static_cast<int>(sumB / n);
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
    m_scaled = m_full.scaled(static_cast<int>(m_full.width() * s),
        static_cast<int>(m_full.height() * s),
        Qt::KeepAspectRatio,
        Qt::SmoothTransformation);
}

void PreviewPanel::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    if (m_hasImage)
        rebuild();
}

void PreviewPanel::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event);
    QPainter painter(this);
    painter.fillRect(rect(), QColor(30, 30, 30));

    if (!m_hasImage)
    {
        painter.setPen(Qt::gray);
        painter.drawText(rect(), Qt::AlignCenter, "未选择图片");
        return;
    }

    const int pad = 10;
    const int txtH = 56;
    const QRect imgArea(pad, pad, width() - pad * 2, height() - pad * 2 - txtH);
    if (!m_scaled.isNull())
    {
        const int x = imgArea.x() + (imgArea.width() - m_scaled.width()) / 2;
        const int y = imgArea.y() + (imgArea.height() - m_scaled.height()) / 2;
        painter.setPen(QColor(0, 0, 0));
        painter.drawRect(x - 1, y - 1, m_scaled.width() + 1, m_scaled.height() + 1);
        painter.drawPixmap(x, y, m_scaled);
    }

    const QRect txtArea(8, height() - txtH - 4, width() - 16, txtH);
    painter.setPen(Qt::white);
    QFont f = painter.font();
    f.setPointSize(9);
    painter.setFont(f);
    const QString name = QFileInfo(m_path).fileName();
    painter.drawText(txtArea,
        Qt::AlignTop | Qt::AlignLeft,
        name + "\n" + QString::number(m_imgW) + "×" + QString::number(m_imgH) + "  " +
            QString::number(m_fileSize / 1024) + " KB");
    painter.setPen(QColor(180, 180, 180));
    painter.drawText(txtArea.adjusted(0, 36, 0, 0),
        Qt::AlignTop | Qt::AlignLeft,
        QString("亮度 %1   RGB(%2,%3,%4)")
            .arg(m_lumMean, 0, 'f', 1)
            .arg(m_rMean)
            .arg(m_gMean)
            .arg(m_bMean));
}
