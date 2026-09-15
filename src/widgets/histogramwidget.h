//
// Copyright (c) 2026 mviewer project. All rights reserved.
// SPDX-License-Identifier: MIT
//
#pragma once

#include <vector>

#include <QWidget>

#include "core/compare/Histogram.h"

class QPainter;

// Overlays the RGB histograms of one or more images for side-by-side compare.
class HistogramWidget : public QWidget
{
    Q_OBJECT

  public:
    explicit HistogramWidget(QWidget *parent = nullptr);

    void setHistograms(const std::vector<mviewer::core::Histogram> &hists);
    void clear();

    int histogramCount() const noexcept
    {
        return static_cast<int>(m_hists.size());
    }
    long histogramTotal(int index) const noexcept
    {
        if (index < 0 || index >= histogramCount())
            return 0;
        return m_hists[static_cast<size_t>(index)].total;
    }

    // M23: channel visibility (R/G/B/Luma/V) and log-scale Y axis.
    // Defaults keep the historical look: RGB on, luma off, V off, linear scale.
    void setChannelVisible(int channel, bool on); // 0=R 1=G 2=B 3=Luma 4=V
    void setLogScale(bool on);
    bool logScale() const
    {
        return m_logScale;
    }
    void setOverlayStyle(bool on);
    bool overlayStyle() const
    {
        return m_overlayStyle;
    }
    void paintOverlay(QPainter &p, const QRect &target) const;

  protected:
    void paintEvent(QPaintEvent *event) override;

  private:
    std::vector<mviewer::core::Histogram> m_hists;
    bool m_chanVisible[5] = {true, true, true, false, false}; // R, G, B, Luma, V
    bool m_logScale = false;
    bool m_overlayStyle = false;
};
