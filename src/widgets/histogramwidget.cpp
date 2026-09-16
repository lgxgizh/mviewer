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
    setMinimumHeight(160);
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
    if (channel < 0 || channel > 4)
        return;
    m_chanVisible[channel] = on;
    update();
}

void HistogramWidget::setLogScale(bool on)
{
    m_logScale = on;
    update();
}

void HistogramWidget::setOverlayStyle(bool on)
{
    if (m_overlayStyle == on)
        return;
    m_overlayStyle = on;
    setAttribute(Qt::WA_TranslucentBackground, on);
    update();
}

void HistogramWidget::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    paintOverlay(p, rect());
}

static void drawHistogramGrid(QPainter &p, double left, double top, int w, int h)
{
    p.save();
    p.setRenderHint(QPainter::Antialiasing, false);
    QPen gridPen(QColor(255, 255, 255, 28), 1, Qt::DashLine);
    gridPen.setCosmetic(true);
    p.setPen(gridPen);
    for (int q = 1; q <= 3; ++q)
    {
        const double gx = left + (static_cast<double>(w) * q) / 4.0;
        p.drawLine(QPointF(gx, top), QPointF(gx, top + h));
    }
    QPen basePen(QColor(255, 255, 255, 50), 1);
    basePen.setCosmetic(true);
    p.setPen(basePen);
    p.drawLine(QPointF(left, top + h - 1), QPointF(left + w, top + h - 1));
    p.restore();
}

void HistogramWidget::paintOverlay(QPainter &p, const QRect &target) const
{
    if (target.width() < 2 || target.height() < 2)
        return;
    if (m_overlayStyle)
        p.fillRect(target, QColor(0, 0, 0, 90));
    else
        p.fillRect(target, QColor(20, 20, 22));

    const int w = target.width();
    const int h = target.height();
    const double left = target.left();
    const double top = target.top();

    drawHistogramGrid(p, left, top, w, h);

    if (m_hists.empty())
        return;

    // Channel accessor: 0=R 1=G 2=B 3=Luma 4=V.
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
        case 3:
            return hist.luma.empty() ? nullptr : &hist.luma;
        case 4:
            return hist.v.empty() ? nullptr : &hist.v;
        default:
            return nullptr;
        }
    };

    // Y mapping: linear or log1p (log keeps small bins visible next to peaks).
    auto mapVal = [this](long v) -> double
    { return m_logScale ? std::log1p(static_cast<double>(v)) : static_cast<double>(v); };

    // Shared scale across every histogram so channels/images are comparable.
    double maxVal = 1.0;
    for (const auto &hist : m_hists)
        for (int c = 0; c < 5; ++c)
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

    // Channel colours: R, G, B, Luma (light gray), V (amber). Low-alpha overlay fills.
    const QColor cols[5] = {QColor(255, 70, 70), QColor(70, 220, 90), QColor(80, 140, 255),
                            QColor(225, 225, 225), QColor(255, 200, 50)};

    for (const auto &hist : m_hists)
    {
        for (int c = 0; c < 5; ++c)
        {
            if (!m_chanVisible[c])
                continue;
            const auto *chPtr = channelOf(hist, c);
            if (!chPtr)
                continue;
            const auto &ch = *chPtr;
            QPolygonF poly;
            poly.reserve(bins + 2);
            QPolygonF linePoly;
            linePoly.reserve(bins);
            poly.append(QPointF(left, top + h));
            for (int i = 0; i < bins; ++i)
            {
                const double x = left + i * dx;
                const double y = top + h - 1 - mapVal(ch[static_cast<size_t>(i)]) * dy;
                const QPointF pt(x, y);
                poly.append(pt);
                linePoly.append(pt);
            }
            poly.append(QPointF(left + w, top + h));

            QColor fill = cols[c];
            fill.setAlpha(55);
            p.setBrush(fill);
            p.setPen(Qt::NoPen);
            p.drawPolygon(poly);

            QColor line = cols[c];
            line.setAlpha(170);
            p.setPen(line);
            p.setBrush(Qt::NoBrush);
            p.drawPolyline(linePoly);
        }
    }
}
