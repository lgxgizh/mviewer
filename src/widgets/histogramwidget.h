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
    explicit HistogramWidget(QWidget* parent = nullptr);

    void setHistograms(const std::vector<mviewer::core::Histogram>& hists);
    void clear();

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    std::vector<mviewer::core::Histogram> m_hists;
};
