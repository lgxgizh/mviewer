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
// Chrome's toolbar controls are ~32–36 px; 34 px is the browse-bar target.
constexpr int kControlHeight = 34;
// Toolbar strips that host kControlHeight controls with vertical padding.
constexpr int kBarHeight = 52;
// Caption status strip. Matches the QStatusBar min/max height in dark.qss.
constexpr int kStatusBarHeight = 28;

constexpr int kRadiusControl = 8;
constexpr int kRadiusPanel = 10;
constexpr int kRadiusChip = 5;

// Font sizes (px). Body is 15 px (~11.25 pt at 96 DPI).
constexpr int kFontCaptionPx = 13;
constexpr int kFontBodyPx = 15;
constexpr int kFontTitlePx = 18;
constexpr int kFontLargeTitlePx = 22;

// Layout defaults handed to Qt through the application style.
constexpr int kWindowMargin = kSpace12;
constexpr int kChildMargin = kSpace8;
constexpr int kLayoutSpacing = kSpace8;

// Dialogs: one 20 px inset, 12 px between groups, right-aligned form labels.
constexpr int kDialogMargin = 20;
constexpr int kDialogSpacing = kSpace12;
constexpr int kFormRowSpacing = 10;
// Popup menu rows. 32 px fits the 15 px body font with a Chrome-like hit target.
constexpr int kMenuItemHeight = 32;

} // namespace mviewer::ui::metrics
