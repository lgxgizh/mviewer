// Preferences round-trip and default for autoCheckUpdates (startup silent update gate).
#include "preferencesdialog.h"

#include <QApplication>
#include <QCheckBox>
#include <QCoreApplication>
#include <QDialogButtonBox>
#include <QPushButton>
#include <QSettings>
#include <QStandardPaths>

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

    QSettings().clear();
    std::cout << (g_failures ? "FAILED" : "PASSED") << " (" << g_failures << " failure(s))\n";
    return g_failures ? 1 : 0;
}
