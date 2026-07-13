#include "analysispanel.h"

#include "core/analysis/AnalysisEngine.h"
#include "core/image/QtConvert.h"

#include <algorithm>

#include <QPainter>

AnalysisPanel::AnalysisPanel(QWidget *parent)
    : QWidget(parent)
{
    setMinimumSize(260, 320);
}

void AnalysisPanel::setImage(const QImage &img)
{
    if (img.isNull()) {
        clear();
        return;
    }
    m_img = img.convertToFormat(QImage::Format_RGB32);
    m_stats = AnalysisEngine::computeStats(mvcore::fromQImage(m_img));
    m_hasImage = true;
    update();
}

void AnalysisPanel::clear()
{
    m_img = QImage();
    m_stats = ImageStats();
    m_hasImage = false;
    update();
}

void AnalysisPanel::setRegionStats(const QString &text)
{
    m_regionText = text;
    update();
}

void AnalysisPanel::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);
    QPainter painter(this);
    painter.fillRect(rect(), QColor(0, 0, 0));

    if (!m_hasImage) {
        painter.setPen(Qt::gray);
        painter.drawText(rect(), Qt::AlignCenter, "未选择图片");
        return;
    }

    const int pad = 12;
    int y = pad;
    const int labelH = 18;
    const int histH = 70;
    const int gap = 10;

    // 亮度直方图
    painter.setPen(QColor(200, 200, 200));
    QFont f = painter.font();
    f.setPointSize(10);
    painter.setFont(f);
    painter.drawText(QRect(pad, y, width() - pad * 2, labelH), Qt::AlignLeft,
                     "亮度直方图");
    y += labelH;
    {
        const QRect bg(pad, y, width() - pad * 2, histH);
        painter.fillRect(bg, QColor(20, 20, 20));
        painter.setPen(QColor(60, 60, 60));
        painter.drawRect(bg);
        drawHistogramChannel(painter, bg, m_stats.histLum, QColor(220, 220, 220));
    }
    y += histH + gap;

    // R 通道直方图
    painter.setPen(QColor(200, 200, 200));
    painter.drawText(QRect(pad, y, width() - pad * 2, labelH), Qt::AlignLeft,
                     "R 通道直方图");
    y += labelH;
    {
        const QRect bg(pad, y, width() - pad * 2, histH);
        painter.fillRect(bg, QColor(20, 20, 20));
        painter.setPen(QColor(60, 60, 60));
        painter.drawRect(bg);
        drawHistogramChannel(painter, bg, m_stats.histR, QColor(230, 60, 60));
    }
    y += histH + gap;

    // G 通道直方图
    painter.setPen(QColor(200, 200, 200));
    painter.drawText(QRect(pad, y, width() - pad * 2, labelH), Qt::AlignLeft,
                     "G 通道直方图");
    y += labelH;
    {
        const QRect bg(pad, y, width() - pad * 2, histH);
        painter.fillRect(bg, QColor(20, 20, 20));
        painter.setPen(QColor(60, 60, 60));
        painter.drawRect(bg);
        drawHistogramChannel(painter, bg, m_stats.histG, QColor(60, 230, 60));
    }
    y += histH + gap;

    // B 通道直方图
    painter.setPen(QColor(200, 200, 200));
    painter.drawText(QRect(pad, y, width() - pad * 2, labelH), Qt::AlignLeft,
                     "B 通道直方图");
    y += labelH;
    {
        const QRect bg(pad, y, width() - pad * 2, histH);
        painter.fillRect(bg, QColor(20, 20, 20));
        painter.setPen(QColor(60, 60, 60));
        painter.drawRect(bg);
        drawHistogramChannel(painter, bg, m_stats.histB, QColor(60, 120, 230));
    }
    y += histH + gap;

    // 统计信息文本
    painter.setPen(QColor(220, 220, 220));
    QFont sf = painter.font();
    sf.setPointSize(9);
    painter.setFont(sf);
    const QString stats =
        QString("亮度均值: %1\n"
                "R 均值: %2   G 均值: %3   B 均值: %4\n"
                "尺寸: %5 × %6")
            .arg(m_stats.lumMean, 0, 'f', 1)
            .arg(m_stats.rMean, 0, 'f', 1)
            .arg(m_stats.gMean, 0, 'f', 1)
            .arg(m_stats.bMean, 0, 'f', 1)
            .arg(m_img.width())
            .arg(m_img.height());
    painter.drawText(QRect(pad, y, width() - pad * 2, labelH * 4),
                     Qt::AlignLeft | Qt::AlignTop, stats);
    y += labelH * 4 + gap;

    // 框选区域统计占位
    painter.setPen(QColor(160, 160, 160));
    painter.drawText(QRect(pad, y, width() - pad * 2, labelH), Qt::AlignLeft,
                     "框选区域统计:");
    y += labelH;
    const QString region =
        m_regionText.isEmpty() ? "（在图像上拖拽框选以查看区域统计）" : m_regionText;
    painter.setPen(QColor(200, 200, 200));
    painter.drawText(QRect(pad, y, width() - pad * 2, labelH * 2),
                     Qt::AlignLeft | Qt::AlignTop, region);
}

void AnalysisPanel::drawHistogramChannel(QPainter &p, const QRect &bg,
                                         const int *hist, const QColor &color)
{
    const int bins = kHistSize;
    const int srcBins = 256;
    const double binW = static_cast<double>(bg.width()) / bins;

    // 聚合到 kHistSize 个 bin,并求最大值用于归一化
    long long agg[kHistSize] = {0};
    long long maxV = 1;
    for (int i = 0; i < bins; ++i) {
        long long sum = 0;
        const int lo = i * srcBins / bins;
        const int hi = (i + 1) * srcBins / bins;
        for (int j = lo; j < hi && j < srcBins; ++j)
            sum += hist[j];
        agg[i] = sum;
        if (sum > maxV)
            maxV = sum;
    }

    p.setPen(color);
    for (int i = 0; i < bins; ++i) {
        const double h = static_cast<double>(agg[i]) / maxV * bg.height();
        const int x = bg.x() + static_cast<int>(i * binW);
        const int hh = std::max(1, static_cast<int>(h));
        const int w = std::max(1, static_cast<int>(binW));
        p.drawLine(x, bg.bottom(), x, bg.bottom() - hh);
        Q_UNUSED(w);
    }
}
