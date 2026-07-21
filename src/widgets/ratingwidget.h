//
// Copyright (c) 2026 mviewer project. All rights reserved.
// SPDX-License-Identifier: MIT
//
#pragma once

#include <QWidget>

class QToolButton;

// Five-star rating editor. Clicking star N sets the rating to N; clicking the
// currently-selected star again clears the rating back to 0.
class RatingWidget : public QWidget
{
    Q_OBJECT

public:
    explicit RatingWidget(QWidget* parent = nullptr);

    int rating() const { return m_rating; }

public slots:
    void setRating(int stars);

signals:
    void ratingChanged(int stars);

private:
    void refresh();

    int m_rating = 0;
    QList<QToolButton*> m_stars;
};
