#pragma once

#include <QWidget>
#include <QImage>
#include <QString>
#include <QRect>

#include "core/analysis/AnalysisEngine.h"

// AnalysisPanel：显示单张图的直方图 + 统计信息 + 框选区域统计
class AnalysisPanel : public QWidget
{
    Q_OBJECT

public:
    explicit AnalysisPanel(QWidget *parent = nullptr);

public slots:
    void setImage(const QImage &img);
    void clear();
    // 框选区域统计(图像坐标系)
    void setRegionStats(const QString &text);

protected:
    void paintEvent(QPaintEvent *) override;

private:
    void drawHistogramChannel(QPainter &p, const QRect &bg, const int *hist, const QColor &color);

    QImage m_img;
    ImageStats m_stats;
    bool m_hasImage = false;
    QString m_regionText;
    static constexpr int kHistSize = 128;
};
