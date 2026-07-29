//
// Copyright (c) 2026 mviewer project. All rights reserved.
// SPDX-License-Identifier: MIT
//
#pragma once

#include <vector>

#include <QWidget>

#include "core/compare/Histogram.h"

// Overlays the RGB histograms of one or more images for side-by-side compare.
class HistogramWidget : public QWidget
{
    Q_OBJECT

  public:
    explicit HistogramWidget(QWidget *parent = nullptr);

    void setHistograms(const std::vector<mviewer::core::Histogram> &hists);
    void clear();

    // M23: channel visibility (R/G/B/Luma) and log-scale Y axis.
    // Defaults keep the historical look: RGB on, luma off, linear scale.
    void setChannelVisible(int channel, bool on); // 0=R 1=G 2=B 3=Luma
    void setLogScale(bool on);
    bool logScale() const
    {
        return m_logScale;
    }

  protected:
    void paintEvent(QPaintEvent *event) override;

  private:
    std::vector<mviewer::core::Histogram> m_hists;
    bool m_chanVisible[4] = {true, true, true, false}; // R, G, B, Luma
    bool m_logScale = false;
};
