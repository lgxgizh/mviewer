//
// Copyright (c) 2026 mviewer project. All rights reserved.
// SPDX-License-Identifier: MIT
//
#include "histogramwidget.h"

#include <QPainter>

HistogramWidget::HistogramWidget(QWidget* parent) : QWidget(parent)
{
    setMinimumHeight(120);
}

void HistogramWidget::setHistograms(const std::vector<mviewer::core::Histogram>& hists)
{
    m_hists = hists;
    update();
}

void HistogramWidget::clear()
{
    m_hists.clear();
    update();
}

void HistogramWidget::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.fillRect(rect(), Qt::black);

    const int w = rect().width();
    const int h = rect().height();
    if (m_hists.empty() || w < 2 || h < 2)
        return;

    // Shared scale across every histogram so channels/images are comparable.
    long maxVal = 1;
    for (const auto& hist : m_hists)
        for (int c = 0; c < 3; ++c)
        {
            const auto& ch = c == 0 ? hist.r : (c == 1 ? hist.g : hist.b);
            for (long v : ch)
                if (v > maxVal)
                    maxVal = v;
        }

    const int bins = m_hists.front().bins;
    const double dx = static_cast<double>(w) / bins;
    const double dy = static_cast<double>(h - 2) / maxVal;

    // Channel colours: R, G, B. Overlay every image with low alpha fills.
    const QColor cols[3] = {QColor(255, 70, 70), QColor(70, 220, 90), QColor(80, 140, 255)};

    for (const auto& hist : m_hists)
    {
        for (int c = 0; c < 3; ++c)
        {
            const auto& ch = c == 0 ? hist.r : (c == 1 ? hist.g : hist.b);
            QPolygonF poly;
            poly.append(QPointF(0, h));
            for (int i = 0; i < bins; ++i)
            {
                const double x = i * dx;
                const double y = h - 1 - ch[static_cast<size_t>(i)] * dy;
                poly.append(QPointF(x, y));
            }
            poly.append(QPointF(w, h));

            QColor fill = cols[c];
            fill.setAlpha(55);
            p.setBrush(fill);
            p.setPen(Qt::NoPen);
            p.drawPolygon(poly);

            QColor line = cols[c];
            line.setAlpha(170);
            p.setPen(line);
            p.setBrush(Qt::NoBrush);
            QPolygonF linePoly;
            for (int i = 0; i < bins; ++i)
            {
                const double x = i * dx;
                const double y = h - 1 - ch[static_cast<size_t>(i)] * dy;
                linePoly.append(QPointF(x, y));
            }
            p.drawPolyline(linePoly);
        }
    }
}
