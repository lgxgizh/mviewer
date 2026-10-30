#pragma once

namespace mviewer::ui::metrics
{

// One spacing / size scale for every panel and dialog. Layout code uses these
// instead of ad-hoc pixel literals; the stylesheet uses the matching tokens.
constexpr int kSpace4 = 4;
constexpr int kSpace8 = 8;
constexpr int kSpace12 = 12;
constexpr int kSpace16 = 16;

// Buttons, combo boxes, line edits and toolbar widgets share one height.
constexpr int kControlHeight = 28;
// Toolbar strips that host kControlHeight controls with kSpace4 padding.
constexpr int kBarHeight = 40;
// Caption status strip. Matches the QStatusBar min/max height in dark.qss.
constexpr int kStatusBarHeight = 22;

constexpr int kRadiusControl = 7;
constexpr int kRadiusPanel = 10;
constexpr int kRadiusChip = 5;

// Font sizes (px). Body is 13 px (~9.75 pt at 96 DPI).
constexpr int kFontCaptionPx = 11;
constexpr int kFontBodyPx = 13;
constexpr int kFontTitlePx = 15;
constexpr int kFontLargeTitlePx = 20;

// Layout defaults handed to Qt through the application style.
constexpr int kWindowMargin = kSpace12;
constexpr int kChildMargin = kSpace8;
constexpr int kLayoutSpacing = kSpace8;

} // namespace mviewer::ui::metrics
