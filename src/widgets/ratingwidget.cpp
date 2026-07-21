//
// Copyright (c) 2026 mviewer project. All rights reserved.
// SPDX-License-Identifier: MIT
//
#include "ratingwidget.h"

#include <QHBoxLayout>
#include <QToolButton>

RatingWidget::RatingWidget(QWidget* parent) : QWidget(parent)
{
    auto* lay = new QHBoxLayout(this);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->setSpacing(0);

    for (int i = 0; i < 5; ++i)
    {
        auto* btn = new QToolButton(this);
        btn->setAutoRaise(true);
        btn->setCheckable(false);
        btn->setText(QString("☆"));
        btn->setToolTip(tr("设为 %1 星").arg(i + 1));
        const int stars = i + 1;
        connect(btn, &QToolButton::clicked, this, [this, stars]() {
            // Toggle off when re-clicking the active top star.
            setRating(m_rating == stars ? 0 : stars);
            emit ratingChanged(m_rating);
        });
        m_stars.append(btn);
        lay->addWidget(btn);
    }
    refresh();
}

void RatingWidget::setRating(int stars)
{
    m_rating = qBound(0, stars, 5);
    refresh();
}

void RatingWidget::refresh()
{
    for (int i = 0; i < m_stars.size(); ++i)
        m_stars[i]->setText(i < m_rating ? QString("★") : QString("☆"));
}
