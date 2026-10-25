// Thin indeterminate loading bar along the top edge of the viewer. Replaces
// the blank canvas while a large image decodes; fast loads never show it.
#include "imageviewer.h"

#include "Theme.h"

#include <QPainter>
#include <QTimer>

namespace
{
constexpr int kBarHeight = 3;
constexpr int kFrameMs = 33;
constexpr int kSweepMs = 1200;
} // namespace

bool ImageViewer::isLoadingBusy() const
{
    return m_loading || m_fullRawLoading || m_largeSourcePending || !m_provisionalImage.isNull();
}

bool ImageViewer::loadingIndicatorVisible() const
{
    return isLoadingBusy() && m_busyClock.isValid() && m_busyClock.elapsed() >= kLoadingBarDelayMs;
}

void ImageViewer::drawLoadingBar(QPainter &painter)
{
    if (!isLoadingBusy())
    {
        m_busyClock.invalidate();
        if (m_busyTicker)
            m_busyTicker->stop();
        return;
    }
    if (!m_busyClock.isValid())
        m_busyClock.start();
    if (!m_busyTicker)
    {
        m_busyTicker = new QTimer(this);
        m_busyTicker->setInterval(kFrameMs);
        connect(m_busyTicker, &QTimer::timeout, this,
                [this]
                {
                    // Stop as soon as the load settles; the next paint clears the bar.
                    if (!isLoadingBusy())
                        m_busyTicker->stop();
                    update();
                });
    }
    if (!m_busyTicker->isActive())
        m_busyTicker->start();

    const qint64 elapsed = m_busyClock.elapsed();
    if (elapsed < kLoadingBarDelayMs || width() <= 0)
        return;

    // A segment a third of the width sweeps left to right, then wraps.
    const int segment = qMax(48, width() / 3);
    const qint64 phase = (elapsed - kLoadingBarDelayMs) % kSweepMs;
    const int x = static_cast<int>((width() + segment) * phase / kSweepMs) - segment;
    QColor track(mviewer::ui::Theme::themeColor(mviewer::ui::ThemeRole::Accent));
    track.setAlpha(48);
    painter.save();
    painter.fillRect(QRect(0, 0, width(), kBarHeight), track);
    painter.fillRect(QRect(x, 0, segment, kBarHeight),
                     QColor(mviewer::ui::Theme::themeColor(mviewer::ui::ThemeRole::Accent)));
    painter.restore();
}
