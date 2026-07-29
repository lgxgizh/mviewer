//
// Copyright (c) 2026 mviewer project. All rights reserved.
// SPDX-License-Identifier: MIT
//
#include "histogramwidget.h"

#include <QPainter>
#include <algorithm>
#include <cmath>

HistogramWidget::HistogramWidget(QWidget *parent) : QWidget(parent)
{
    setMinimumHeight(120);
}

void HistogramWidget::setHistograms(const std::vector<mviewer::core::Histogram> &hists)
{
    m_hists = hists;
    update();
}

void HistogramWidget::clear()
{
    m_hists.clear();
    update();
}

void HistogramWidget::setChannelVisible(int channel, bool on)
{
    if (channel < 0 || channel > 3)
        return;
    m_chanVisible[channel] = on;
    update();
}

void HistogramWidget::setLogScale(bool on)
{
    m_logScale = on;
    update();
}

void HistogramWidget::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.fillRect(rect(), Qt::black);

    const int w = rect().width();
    const int h = rect().height();
    if (m_hists.empty() || w < 2 || h < 2)
        return;

    // Channel accessor: 0=R 1=G 2=B 3=Luma (luma may be absent on old data).
    auto channelOf = [](const mviewer::core::Histogram &hist, int c) -> const std::vector<long> *
    {
        switch (c)
        {
        case 0:
            return &hist.r;
        case 1:
            return &hist.g;
        case 2:
            return &hist.b;
        default:
            return hist.luma.empty() ? nullptr : &hist.luma;
        }
    };

    // Y mapping: linear or log1p (log keeps small bins visible next to peaks).
    auto mapVal = [this](long v) -> double
    { return m_logScale ? std::log1p(static_cast<double>(v)) : static_cast<double>(v); };

    // Shared scale across every histogram so channels/images are comparable.
    double maxVal = 1.0;
    for (const auto &hist : m_hists)
        for (int c = 0; c < 4; ++c)
        {
            if (!m_chanVisible[c])
                continue;
            const auto *ch = channelOf(hist, c);
            if (!ch)
                continue;
            for (long v : *ch)
                maxVal = std::max(maxVal, mapVal(v));
        }

    const int bins = m_hists.front().bins;
    const double dx = static_cast<double>(w) / bins;
    const double dy = static_cast<double>(h - 2) / maxVal;

    // Channel colours: R, G, B, Luma (light gray). Low-alpha overlay fills.
    const QColor cols[4] = {QColor(255, 70, 70), QColor(70, 220, 90), QColor(80, 140, 255),
                            QColor(225, 225, 225)};

    for (const auto &hist : m_hists)
    {
        for (int c = 0; c < 4; ++c)
        {
            if (!m_chanVisible[c])
                continue;
            const auto *chPtr = channelOf(hist, c);
            if (!chPtr)
                continue;
            const auto &ch = *chPtr;
            QPolygonF poly;
            poly.append(QPointF(0, h));
            for (int i = 0; i < bins; ++i)
            {
                const double x = i * dx;
                const double y = h - 1 - mapVal(ch[static_cast<size_t>(i)]) * dy;
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
                const double y = h - 1 - mapVal(ch[static_cast<size_t>(i)]) * dy;
                linePoly.append(QPointF(x, y));
            }
            p.drawPolyline(linePoly);
        }
    }
}
