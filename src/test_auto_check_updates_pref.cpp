// Preferences round-trip and default for autoCheckUpdates (startup silent update gate).
#include "platform/WindowChrome.h"
#include "preferencesdialog.h"
#include "thumbnailcard_style.h"

#include <QApplication>
#include <QCheckBox>
#include <QCoreApplication>
#include <QDialogButtonBox>
#include <QImage>
#include <QPainter>
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

    // Windows 11 translucent sidebar (Mica) toggle: on by default, persisted.
    // Earlier PreferencesDialog::accept already wrote the key (checkbox default-
    // checked), so clear it before asserting the empty-settings default.
    using mviewer::platform::WindowChrome;
    const QString micaKey = QString::fromLatin1(WindowChrome::translucentSidebarSettingsKey());
    QSettings().remove(micaKey);
    check(!QSettings().contains(micaKey), "translucent sidebar key absent after clear");
    check(WindowChrome::translucentSidebarEnabled(), "translucent sidebar defaults on");
    {
        PreferencesDialog dlg;
        auto *box = dlg.findChild<QCheckBox *>(QStringLiteral("preferencesTranslucentSidebar"));
        check(box != nullptr, "General tab exposes preferencesTranslucentSidebar");
        if (box)
        {
            check(box->isChecked(), "translucent sidebar checkbox starts checked");
            box->setChecked(false);
            check(clickOk(dlg), "OK turns translucent sidebar off");
        }
    }
    check(QSettings().contains(micaKey) && !WindowChrome::translucentSidebarEnabled(),
          "unchecked translucent sidebar persists false");
    {
        QWidget window;
        WindowChrome::prepareMainWindow(&window);
        check(!WindowChrome::micaActive(&window), "Mica stays off while the preference is off");
    }
    {
        PreferencesDialog dlg;
        auto *box = dlg.findChild<QCheckBox *>(QStringLiteral("preferencesTranslucentSidebar"));
        check(box != nullptr && !box->isChecked(),
              "reopened Preferences reflects translucent sidebar off");
        if (box)
        {
            box->setChecked(true);
            check(clickOk(dlg), "OK turns translucent sidebar back on");
        }
    }
    check(WindowChrome::translucentSidebarEnabled(), "checked translucent sidebar persists true");
    {
        // Ghosting regression (2026-11-01): the main window must never become
        // a translucent top-level, even with Mica on; only the title bar uses it.
        QWidget window;
        WindowChrome::prepareMainWindow(&window);
        check(!window.testAttribute(Qt::WA_TranslucentBackground),
              "prepareMainWindow keeps the client area opaque");
    }

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
    if (!WindowChrome::currentFeatures().mica)
    {
        QWidget window;
        WindowChrome::prepareMainWindow(&window);
        check(!WindowChrome::micaActive(&window), "no Mica on an OS without DWM backdrops");
    }

    {
        // Light theme browse list/details: QSS often leaves QPalette::Text white
        // on QListView even when Base is light (#285 only tested a healthy
        // palette, so list rows stayed unreadable). itemTextColor must still
        // return dark ink; drawNameText must paint it.
        QPalette polluted;
        polluted.setColor(QPalette::Text, QColor(0xff, 0xff, 0xff)); // onAccent leak
        polluted.setColor(QPalette::WindowText, QColor(0xff, 0xff, 0xff));
        polluted.setColor(QPalette::Base, QColor(0xfb, 0xfb, 0xfd));
        polluted.setColor(QPalette::AlternateBase, QColor(0xf5, 0xf5, 0xf7));
        polluted.setColor(QPalette::Highlight, QColor(0x00, 0x7a, 0xff));
        polluted.setColor(QPalette::HighlightedText, QColor(0xff, 0xff, 0xff));

        const QColor unselected = thumb_card::itemTextColor(false, polluted);
        check(unselected.lightness() < 80, "unselected list ink is dark on light Base");
        check(unselected == QColor(0x1d, 0x1d, 0x1f), "unselected list ink is #1d1d1f");
        check(!thumb_card::isDarkPalette(polluted), "light Base is not classified dark");
        const QColor selected = thumb_card::itemTextColor(true, polluted);
        check(selected.lightness() > 200, "selected list ink stays light on accent");

        // Secondary caption: same channel mix as drawThumbFooter (alpha 100/255).
        const QColor captionBase = polluted.color(QPalette::Base);
        const int mix = 100;
        QColor caption = unselected;
        caption.setRed((unselected.red() * (255 - mix) + captionBase.red() * mix) / 255);
        caption.setGreen((unselected.green() * (255 - mix) + captionBase.green() * mix) / 255);
        caption.setBlue((unselected.blue() * (255 - mix) + captionBase.blue() * mix) / 255);
        check(caption.lightness() < 200, "secondary caption blend stays readable on light Base");

        QImage img(160, 24, QImage::Format_ARGB32_Premultiplied);
        img.fill(polluted.color(QPalette::Base));
        {
            QPainter p(&img);
            thumb_card::drawNameText(&p, img.rect(), QStringLiteral("IMG_0001.jpeg"), false,
                                     polluted);
        }
        const QRgb base = polluted.color(QPalette::Base).rgb();
        int darkest = 255;
        bool inked = false;
        for (int y = 0; y < img.height(); ++y)
            for (int x = 0; x < img.width(); ++x)
            {
                const QRgb px = img.pixel(x, y);
                inked = inked || px != base;
                darkest = qMin(darkest, QColor(px).lightness());
            }
        if (inked)
            check(darkest < 120, "unselected light-theme file name is drawn dark");
        else
            std::cout << "[skip] no font available to draw the file name\n";

        // Dark theme polluted the other way (dark Text on dark Base) still inks light.
        QPalette darkPolluted;
        darkPolluted.setColor(QPalette::Text, QColor(0x1d, 0x1d, 0x1f));
        darkPolluted.setColor(QPalette::Base, QColor(0x1c, 0x1c, 0x1e));
        darkPolluted.setColor(QPalette::HighlightedText, QColor(0xff, 0xff, 0xff));
        const QColor darkInk = thumb_card::itemTextColor(false, darkPolluted);
        check(darkInk.lightness() > 200, "unselected dark-theme ink is light");
        check(thumb_card::isDarkPalette(darkPolluted), "dark Base is classified dark");
    }

    QSettings().clear();
    std::cout << (g_failures ? "FAILED" : "PASSED") << " (" << g_failures << " failure(s))\n";
    return g_failures ? 1 : 0;
}
