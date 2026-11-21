// Corner-slot layout for the floating overlays (review: 4 overlay collisions).
//
// Before this, each panel positioned itself at a hard-coded offset from the
// corner at the moment it was created. Two of them shared the bottom-left
// corner (the scope widget drew straight over the frame-status pill) and none of
// them moved on resize or on F11 fullscreen, so they drifted or flew off screen.
//
// Now every floating widget belongs to exactly one corner slot, and this single
// function is the only place that decides where a slot is. The canvas-drawn
// chips (histogram / RAW / overlay badges top-left, frame pill bottom-left,
// zoom / EXIF / FFT readouts top-right) keep their corners: the widget slots are
// inset to leave that room instead of covering them.

#include "imageviewer.h"

#include "widgets/displayadjustpanel.h"
#include "widgets/scopewidget.h"

#include <algorithm>

namespace
{
// Canvas-drawn chips that own a corner. The widget slots step aside for them.
constexpr int kMargin = 12;
constexpr int kTopChipHeight = 108;   // histogram card + RAW / overlay badges
constexpr int kBottomChipHeight = 36; // frame-status pill
constexpr int kSlotGap = 8;
} // namespace

void ImageViewer::layoutFloatingOverlays()
{
    if (width() <= 0 || height() <= 0)
        return;

    // ── Bottom-left: the scope widget, lifted clear of the frame pill ───────
    if (m_scopeWidget && m_scopeWidget->isVisible())
    {
        const int w = std::min(320, std::max(160, width() / 3));
        const int h = std::min(220, std::max(120, height() / 3));
        const int bottom = height() - kMargin - kBottomChipHeight;
        const int top = std::max(kMargin + kTopChipHeight, bottom - h);
        m_scopeWidget->setGeometry(kMargin, std::min(top, bottom - h), w, h);
        m_scopeWidget->raise();
    }

    // ── Top-right: the FFT spectrum panel ───────────────────────────────────
    if (m_spectrumPanel && m_spectrumPanel->isVisible())
    {
        const int edge = std::min(240, std::max(160, width() / 4));
        m_spectrumPanel->setGeometry(width() - edge - kMargin, kMargin, edge, edge + 16);
        m_spectrumPanel->raise();
    }

    // ── Bottom-right: the display-adjustment card ───────────────────────────
    if (m_adjustPanel && m_adjustPanel->isVisible())
    {
        const int w = std::min(280, std::max(200, width() / 4));
        const int h = std::min(240, std::max(140, height() / 3));
        m_adjustPanel->setGeometry(width() - w - kMargin,
                                   std::max(kMargin + kTopChipHeight, height() - h - kMargin), w,
                                   h);
        m_adjustPanel->raise();
    }
}
