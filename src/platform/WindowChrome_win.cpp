// Windows DWM window chrome. Compiled on Windows only; see WindowChrome.cpp
// for the portable parts and the non-Windows no-op stubs.
#include "platform/WindowChrome.h"

#include <QtGlobal>

#if defined(Q_OS_WIN)

#include "Theme.h"

#include <QApplication>
#include <QColor>
#include <QEvent>
#include <QGuiApplication>
#include <QMenu>
#include <QStyle>
#include <QVariant>
#include <QWidget>
#include <QWindow>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <dwmapi.h>

namespace mviewer::platform
{

namespace
{

// Values from the Windows 11 SDK; older SDK headers may not define them.
constexpr DWORD kDwmUseImmersiveDarkMode = 20;
constexpr DWORD kDwmUseImmersiveDarkModeLegacy = 19; // Windows 10 before 20H1
constexpr DWORD kDwmWindowCornerPreference = 33;
constexpr DWORD kDwmBorderColor = 34;
constexpr DWORD kDwmCaptionColor = 35;
constexpr DWORD kDwmTextColor = 36;
constexpr DWORD kDwmSystemBackdropType = 38;
constexpr DWORD kDwmColorDefault = 0xFFFFFFFF;
constexpr int kCornerRound = 2;        // DWMWCP_ROUND
constexpr int kCornerRoundSmall = 3;   // DWMWCP_ROUNDSMALL
constexpr int kBackdropMainWindow = 2; // DWMSBT_MAINWINDOW (Mica)

template <typename T> bool setAttribute(HWND hwnd, DWORD attribute, const T &value)
{
    return SUCCEEDED(DwmSetWindowAttribute(hwnd, attribute, &value, sizeof(value)));
}

COLORREF toColorRef(const QColor &color)
{
    return RGB(color.red(), color.green(), color.blue());
}

QColor tokenColor(mviewer::ui::ThemeRole role)
{
    return QColor(mviewer::ui::Theme::themeColor(role));
}

bool activeThemeIsDark()
{
    return tokenColor(mviewer::ui::ThemeRole::Bg1).lightness() < 128;
}

HWND nativeHandle(QWidget *widget)
{
    // Offscreen/minimal test platforms hand out fake ids, not HWNDs.
    if (QGuiApplication::platformName() != QLatin1String("windows"))
        return nullptr;
    if (widget == nullptr || !widget->isWindow() || widget->windowHandle() == nullptr)
        return nullptr;
    return reinterpret_cast<HWND>(widget->winId());
}

bool isPopupLike(const QWidget *widget)
{
    const Qt::WindowType type = widget->windowType();
    return type == Qt::Popup || type == Qt::ToolTip;
}

void applyPopupChrome(QWidget *widget)
{
    const WindowChromeFeatures features = WindowChrome::currentFeatures();
    if (!features.roundedCorners)
        return;
    HWND hwnd = nativeHandle(widget);
    if (hwnd == nullptr)
        return;
    if (!setAttribute(hwnd, kDwmWindowCornerPreference, kCornerRoundSmall))
        return;
    // DWM now clips and outlines the menu; drop the QSS border and radius so
    // no square corner or double edge shows inside the system shape.
    if (auto *menu = qobject_cast<QMenu *>(widget); menu && !menu->property("winRounded").toBool())
    {
        menu->setProperty("winRounded", true);
        menu->style()->unpolish(menu);
        menu->style()->polish(menu);
        menu->update();
    }
}

void applyWindowChrome(QWidget *widget, bool forceFrameRefresh)
{
    HWND hwnd = nativeHandle(widget);
    if (hwnd == nullptr)
        return;
    const WindowChromeFeatures features = WindowChrome::currentFeatures();
    const bool mica = features.mica && WindowChrome::micaActive(widget);

    if (features.immersiveDarkMode)
    {
        const BOOL dark = activeThemeIsDark() ? TRUE : FALSE;
        if (!setAttribute(hwnd, kDwmUseImmersiveDarkMode, dark))
            setAttribute(hwnd, kDwmUseImmersiveDarkModeLegacy, dark);
    }
    if (features.roundedCorners)
        setAttribute(hwnd, kDwmWindowCornerPreference, kCornerRound);
    if (features.captionColor)
    {
        // With Mica the caption keeps the system default so the backdrop
        // shows through; otherwise it matches the toolbar background token.
        const DWORD caption =
            mica ? kDwmColorDefault : toColorRef(tokenColor(mviewer::ui::ThemeRole::Bg1));
        setAttribute(hwnd, kDwmCaptionColor, caption);
        setAttribute(hwnd, kDwmTextColor,
                     toColorRef(tokenColor(mviewer::ui::ThemeRole::TextPrimary)));
        setAttribute(hwnd, kDwmBorderColor, toColorRef(tokenColor(mviewer::ui::ThemeRole::Bg3)));
    }
    if (mica)
    {
        const MARGINS margins{-1, -1, -1, -1};
        if (SUCCEEDED(DwmExtendFrameIntoClientArea(hwnd, &margins)))
            setAttribute(hwnd, kDwmSystemBackdropType, kBackdropMainWindow);
    }
    if (forceFrameRefresh)
    {
        // Windows 10 repaints the caption only on activation; nudge the frame.
        SetWindowPos(hwnd, nullptr, 0, 0, 0, 0,
                     SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE |
                         SWP_NOOWNERZORDER);
    }
}

class ChromeFilter : public QObject
{
  public:
    using QObject::QObject;

  protected:
    bool eventFilter(QObject *watched, QEvent *event) override
    {
        if (event->type() == QEvent::Show && watched->isWidgetType())
        {
            auto *widget = static_cast<QWidget *>(watched);
            if (widget->isWindow())
            {
                if (isPopupLike(widget))
                    applyPopupChrome(widget);
                else
                    applyWindowChrome(widget, false);
            }
        }
        return QObject::eventFilter(watched, event);
    }
};

void installChromeFilter()
{
    static ChromeFilter *filter = nullptr;
    if (filter != nullptr || qApp == nullptr)
        return;
    filter = new ChromeFilter(qApp);
    qApp->installEventFilter(filter);
}

} // namespace

void WindowChrome::prepareMainWindow(QWidget *window)
{
    installChromeFilter();
    if (window == nullptr || !currentFeatures().mica || !translucentSidebarEnabled())
        return;
    // Mica draws behind the client area only where Qt leaves pixels
    // transparent. The QSS [mica="true"] rules clear the toolbar and sidebar;
    // the gallery, viewer and compare canvases keep painting opaque.
    window->setAttribute(Qt::WA_TranslucentBackground, true);
    window->setProperty("mica", true);
}

void WindowChrome::themeChanged()
{
    installChromeFilter();
    if (qApp == nullptr)
        return;
    const auto windows = QApplication::topLevelWidgets();
    for (QWidget *widget : windows)
    {
        if (widget->isVisible() && !isPopupLike(widget))
            applyWindowChrome(widget, true);
    }
}

} // namespace mviewer::platform

#endif // Q_OS_WIN
