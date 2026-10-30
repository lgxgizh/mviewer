#include "platform/WindowChrome.h"

#include <QOperatingSystemVersion>
#include <QSettings>
#include <QVariant>
#include <QWidget>

namespace mviewer::platform
{

namespace
{
constexpr int kBuildDarkMode = 17763; // Windows 10 1809
constexpr int kBuildWin11 = 22000;
constexpr int kBuildWin11_22H2 = 22621;
constexpr const char *kTranslucentKey = "ui/translucentSidebar";
} // namespace

WindowChromeFeatures WindowChrome::featuresForBuild(int windowsBuild)
{
    WindowChromeFeatures f;
    f.immersiveDarkMode = windowsBuild >= kBuildDarkMode;
    f.roundedCorners = windowsBuild >= kBuildWin11;
    f.captionColor = windowsBuild >= kBuildWin11;
    f.mica = windowsBuild >= kBuildWin11_22H2;
    return f;
}

WindowChromeFeatures WindowChrome::currentFeatures()
{
#ifdef Q_OS_WIN
    const QOperatingSystemVersion os = QOperatingSystemVersion::current();
    if (os.type() != QOperatingSystemVersion::Windows || os.majorVersion() < 10)
        return {};
    return featuresForBuild(os.microVersion());
#else
    return {};
#endif
}

const char *WindowChrome::translucentSidebarSettingsKey()
{
    return kTranslucentKey;
}

bool WindowChrome::translucentSidebarEnabled()
{
    // On by default; only Windows 11 22H2+ acts on it. Turning it off in
    // Preferences restores the solid toolbar and sidebar after a restart.
    return QSettings().value(QLatin1String(kTranslucentKey), true).toBool();
}

void WindowChrome::setTranslucentSidebarEnabled(bool enabled)
{
    QSettings settings;
    settings.setValue(QLatin1String(kTranslucentKey), enabled);
}

bool WindowChrome::micaActive(const QWidget *window)
{
    return window != nullptr && window->property("mica").toBool();
}

#ifndef Q_OS_WIN
void WindowChrome::prepareMainWindow(QWidget *)
{
}

void WindowChrome::themeChanged()
{
}
#endif

} // namespace mviewer::platform
