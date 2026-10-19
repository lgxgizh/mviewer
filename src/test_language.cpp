// UI language switching: English is the source language; zh_CN comes from the
// embedded translator and the localized help pages. This file is on the
// i18n gate allowlist because it asserts the Chinese translation itself.

#include "Language.h"
#include "compareworkspace_shortcuts.h"
#include "fileopmessages.h"

#include <QApplication>
#include <QCoreApplication>
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

QString tr(const char *context, const char *text)
{
    return QCoreApplication::translate(context, text);
}
} // namespace

int main(int argc, char **argv)
{
    QStandardPaths::setTestModeEnabled(true);
    QApplication app(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("MViewerTest"));
    QCoreApplication::setApplicationName(QStringLiteral("language_tests"));
    QSettings().clear();

    using mviewer::ui::Language;
    using mviewer::ui::LanguageMode;

    Language::apply(LanguageMode::English);
    check(!Language::effectiveChinese(), "English mode is not Chinese");
    check(tr("MainWindow", "&File") == QStringLiteral("&File"), "English menu uses source text");
    check(mviewer::cw::compareShortcutTr("Flicker") == QStringLiteral("Flicker"),
          "English compare shortcut uses source text");
    const QString guideEn = Language::localizedHelpHtml(QStringLiteral("guide"));
    const QString keysEn = Language::localizedHelpHtml(QStringLiteral("shortcuts"));
    check(guideEn.contains(QStringLiteral("integer-pixel")), "English user guide is loaded");
    check(keysEn.contains(QStringLiteral("Checkerboard")), "English shortcut page is loaded");
    check(QSettings().value(QStringLiteral("uiLanguage")).toString() == QStringLiteral("en"),
          "English preference is persisted as \"en\"");

    Language::apply(LanguageMode::ChineseSimplified);
    check(Language::effectiveChinese(), "Chinese mode is Chinese");
    check(tr("MainWindow", "&File") == QStringLiteral("文件(&F)"), "Chinese menu is translated");
    check(mviewer::cw::compareShortcutTr("Flicker") == QStringLiteral("闪烁"),
          "Chinese compare shortcut is translated");
    check(mviewer::ui::fileOpErrorText(
              QStringLiteral("Some files remain in trash; use Undo to retry recovery.")) ==
              QStringLiteral("部分文件仍在回收站中，请使用撤销重试恢复。"),
          "Chinese file-operation error is translated");
    const QString guideZh = Language::localizedHelpHtml(QStringLiteral("guide"));
    check(!guideZh.isEmpty() && guideZh != guideEn && guideZh.contains(QStringLiteral("比较")),
          "Chinese user guide is loaded");
    check(Language::localizedHelpHtml(QStringLiteral("shortcuts")) != keysEn,
          "Chinese shortcut page is loaded");
    check(Language::localizedHelpHtml(QStringLiteral("no-such-page")).isEmpty(),
          "missing help page returns empty");

    Language::apply(LanguageMode::English);
    check(tr("MainWindow", "&File") == QStringLiteral("&File"),
          "switching back to English removes the translator");

    QSettings().clear();
    std::cout << (g_failures ? "FAILED" : "PASSED") << " (" << g_failures << " failure(s))\n";
    return g_failures ? 1 : 0;
}
