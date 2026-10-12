//
// Copyright (c) 2026 mviewer project. All rights reserved.
// SPDX-License-Identifier: MIT
//
// One place that decides "which digit did the user press" for shortcuts.
// With Shift held, Windows reports the shifted symbol (Shift+1 -> Key_Exclam,
// Shift+2 -> Key_At, ...) instead of Key_1..Key_9, so every Shift+digit /
// Ctrl+Shift+digit shortcut that compared against Qt::Key_0..9 silently never
// fired there. The virtual key (VK '0'..'9') is layout independent, and the
// US shifted symbols are a fallback when no native key is available.
#pragma once

#include "core/analysis/ImageOverlay.h"

#include <QKeyEvent>

namespace mviewer::ui
{

// 0..9 for a digit key in any form, -1 otherwise.
constexpr int shortcutDigit(int qtKey, Qt::KeyboardModifiers modifiers, quint32 nativeVirtualKey)
{
    if (qtKey >= Qt::Key_0 && qtKey <= Qt::Key_9)
        return qtKey - Qt::Key_0;
    if (!(modifiers & Qt::ShiftModifier))
        return -1;
    if (nativeVirtualKey >= 0x30U && nativeVirtualKey <= 0x39U)
        return static_cast<int>(nativeVirtualKey - 0x30U);
    switch (qtKey)
    {
    case Qt::Key_ParenRight:
        return 0;
    case Qt::Key_Exclam:
        return 1;
    case Qt::Key_At:
        return 2;
    case Qt::Key_NumberSign:
        return 3;
    case Qt::Key_Dollar:
        return 4;
    case Qt::Key_Percent:
        return 5;
    case Qt::Key_AsciiCircum:
        return 6;
    case Qt::Key_Ampersand:
        return 7;
    case Qt::Key_Asterisk:
        return 8;
    case Qt::Key_ParenLeft:
        return 9;
    default:
        return -1;
    }
}

inline int shortcutDigit(const QKeyEvent *event)
{
    return event ? shortcutDigit(event->key(), event->modifiers(), event->nativeVirtualKey()) : -1;
}

// Qt::Key_0..Key_9 for a digit event in any form, otherwise event->key().
inline int normalizedShortcutKey(const QKeyEvent *event)
{
    const int digit = shortcutDigit(event);
    return digit >= 0 ? Qt::Key_0 + digit : (event ? event->key() : 0);
}

// Shift+0..6 channel isolation, shared by the single viewer and Compare:
// 0 and 1 show RGB, 2..6 isolate R, G, B, Y (luma) and V (value).
constexpr mviewer::OverlayMode channelOverlayForDigit(int digit)
{
    switch (digit)
    {
    case 2:
        return mviewer::OverlayMode::ChannelR;
    case 3:
        return mviewer::OverlayMode::ChannelG;
    case 4:
        return mviewer::OverlayMode::ChannelB;
    case 5:
        return mviewer::OverlayMode::ChannelY;
    case 6:
        return mviewer::OverlayMode::ChannelV;
    default:
        return mviewer::OverlayMode::None;
    }
}

} // namespace mviewer::ui
