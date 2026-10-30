#pragma once

class QWidget;

namespace mviewer::platform
{

// Native window chrome on Windows (DWM): dark/light title bar that follows the
// app theme, Windows 11 rounded corners, caption/border color matched to the
// toolbar token, small rounded corners on popup menus, and an optional Mica
// backdrop behind the toolbar and sidebar. Every call is a no-op on other
// platforms and every DWM failure is ignored (older Windows keeps solid colors).
struct WindowChromeFeatures
{
    bool immersiveDarkMode = false; // Windows 10 1809+ (build 17763)
    bool roundedCorners = false;    // Windows 11 (build 22000)
    bool captionColor = false;      // Windows 11 (build 22000)
    bool mica = false;              // Windows 11 22H2 (build 22621)
};

class WindowChrome
{
  public:
    // Pure mapping from a Windows build number to what DWM supports.
    static WindowChromeFeatures featuresForBuild(int windowsBuild);
    // Features for the running OS (all false off Windows).
    static WindowChromeFeatures currentFeatures();

    // Preferences: "Use translucent sidebar (Windows 11)". Persisted in
    // QSettings; read once when the main window is created.
    static bool translucentSidebarEnabled();
    static void setTranslucentSidebarEnabled(bool enabled);
    static const char *translucentSidebarSettingsKey();

    // Call before the main window is first shown. Opts it into Mica when the
    // OS supports it and the preference is on; otherwise does nothing.
    static void prepareMainWindow(QWidget *window);
    // True when prepareMainWindow enabled Mica on this window.
    static bool micaActive(const QWidget *window);

    // Installs the app-wide show filter once and re-applies the title bar
    // colors to every open window. Theme::applyTheme calls this.
    static void themeChanged();
};

} // namespace mviewer::platform
