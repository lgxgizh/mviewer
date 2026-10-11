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

constexpr int kRadiusControl = 6;
constexpr int kRadiusPanel = 8;

// Font sizes (px) for rich-text / inline stylesheet text.
constexpr int kFontCaptionPx = 11;
constexpr int kFontBodyPx = 12;
constexpr int kFontTitlePx = 15;

// Layout defaults handed to Qt through the application style.
constexpr int kWindowMargin = kSpace12;
constexpr int kChildMargin = kSpace8;
constexpr int kLayoutSpacing = kSpace8;

} // namespace mviewer::ui::metrics
