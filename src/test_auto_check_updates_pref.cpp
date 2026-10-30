// Preferences round-trip and default for autoCheckUpdates (startup silent update gate).
#include "platform/WindowChrome.h"
#include "preferencesdialog.h"

#include <QApplication>
#include <QCheckBox>
#include <QCoreApplication>
#include <QDialogButtonBox>
#include <QPushButton>
#include <QSettings>
#include <QStandardPaths>
#include <QWidget>

#include <iostream>

namespace
{
int g_failures = 0;

void check(bool condition, const char *message)
{
    std::cout << (condition ? "[ok] " : "[FAIL] ") << message << "\n";
    if (!condition)
        ++g_failures;
}

bool clickOk(PreferencesDialog &dlg)
{
    auto *buttons = dlg.findChild<QDialogButtonBox *>();
    if (!buttons)
        return false;
    if (auto *ok = buttons->button(QDialogButtonBox::Ok))
    {
        ok->click();
        return true;
    }
    return false;
}
} // namespace

int main(int argc, char **argv)
{
    QStandardPaths::setTestModeEnabled(true);
    QApplication app(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("MViewerTest"));
    QCoreApplication::setApplicationName(QStringLiteral("auto_check_updates_pref_tests"));
    QSettings().clear();

    check(!QSettings().contains(QStringLiteral("autoCheckUpdates")),
          "fresh settings have no autoCheckUpdates key");
    check(QSettings().value(QStringLiteral("autoCheckUpdates"), true).toBool(),
          "unset autoCheckUpdates defaults to true (startup check stays on)");

    {
        PreferencesDialog dlg;
        auto *box = dlg.findChild<QCheckBox *>(QStringLiteral("preferencesAutoCheckUpdates"));
        check(box != nullptr, "General tab exposes preferencesAutoCheckUpdates");
        if (box)
        {
            check(box->isChecked(), "checkbox defaults checked when key is unset");
            box->setChecked(false);
            check(clickOk(dlg), "OK applies Preferences");
        }
    }

    check(QSettings().contains(QStringLiteral("autoCheckUpdates")),
          "accept persists autoCheckUpdates");
    check(!QSettings().value(QStringLiteral("autoCheckUpdates")).toBool(),
          "unchecked checkbox persists false");

    {
        PreferencesDialog dlg;
        auto *box = dlg.findChild<QCheckBox *>(QStringLiteral("preferencesAutoCheckUpdates"));
        check(box != nullptr && !box->isChecked(),
              "reopened Preferences reflects autoCheckUpdates=false");
        if (box)
        {
            box->setChecked(true);
            check(clickOk(dlg), "OK re-enables autoCheckUpdates");
        }
    }
    check(QSettings().value(QStringLiteral("autoCheckUpdates")).toBool(),
          "re-enabling the checkbox persists true");

    // Windows 11 translucent sidebar (Mica) toggle: opt-in, persisted.
    using mviewer::platform::WindowChrome;
    const QString micaKey = QString::fromLatin1(WindowChrome::translucentSidebarSettingsKey());
    check(!WindowChrome::translucentSidebarEnabled(), "translucent sidebar defaults off");
    {
        PreferencesDialog dlg;
        auto *box = dlg.findChild<QCheckBox *>(QStringLiteral("preferencesTranslucentSidebar"));
        check(box != nullptr, "General tab exposes preferencesTranslucentSidebar");
        if (box)
        {
            check(!box->isChecked(), "translucent sidebar checkbox starts unchecked");
            box->setChecked(true);
            check(clickOk(dlg), "OK applies translucent sidebar");
        }
    }
    check(QSettings().value(micaKey).toBool() && WindowChrome::translucentSidebarEnabled(),
          "checked translucent sidebar persists true");
    {
        PreferencesDialog dlg;
        auto *box = dlg.findChild<QCheckBox *>(QStringLiteral("preferencesTranslucentSidebar"));
        check(box != nullptr && box->isChecked() && box->isEnabled(),
              "reopened Preferences reflects translucent sidebar on and lets it be turned off");
        if (box)
        {
            box->setChecked(false);
            check(clickOk(dlg), "OK turns translucent sidebar off");
        }
    }
    check(!WindowChrome::translucentSidebarEnabled(), "unchecked translucent sidebar persists");

    // Build gates: Win10 gets the dark title bar only; Win11 adds corners and
    // caption color; 22H2 adds Mica.
    const auto win10 = WindowChrome::featuresForBuild(19045);
    check(win10.immersiveDarkMode && !win10.roundedCorners && !win10.captionColor && !win10.mica,
          "Windows 10 build: dark title bar only");
    const auto win11 = WindowChrome::featuresForBuild(22000);
    check(win11.roundedCorners && win11.captionColor && !win11.mica,
          "Windows 11 21H2: corners and caption color, no Mica");
    check(WindowChrome::featuresForBuild(22621).mica, "Windows 11 22H2: Mica available");
    check(!WindowChrome::featuresForBuild(0).immersiveDarkMode, "unknown build: nothing");
#ifndef Q_OS_WIN
    check(!WindowChrome::currentFeatures().mica && !WindowChrome::currentFeatures().roundedCorners,
          "non-Windows reports no DWM features");
#endif
    {
        QWidget window;
        WindowChrome::prepareMainWindow(&window);
        check(!WindowChrome::micaActive(&window), "Mica stays off while the preference is off");
    }

    QSettings().clear();
    std::cout << (g_failures ? "FAILED" : "PASSED") << " (" << g_failures << " failure(s))\n";
    return g_failures ? 1 : 0;
}
