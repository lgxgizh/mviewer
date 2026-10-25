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
#include <QTranslator>

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

    // The shipped catalog is committed (the build does not run lrelease). These
    // strings are recent or added here, so a stale .qm fails this gate.
    QTranslator embedded;
    check(embedded.load(QStringLiteral(":/i18n/mviewer_zh_CN.qm")),
          "embedded :/i18n/mviewer_zh_CN.qm loads");
    check(embedded.translate("MainWindow", "Close Split Browse View") ==
              QStringLiteral("关闭双栏浏览"),
          "qm translates Close Split Browse View");
    check(embedded.translate("MainWindow", "Focus Browse Mode (F6)") ==
              QStringLiteral("专注浏览模式 (F6)"),
          "qm translates Focus Browse Mode");
    check(embedded.translate("ThumbnailPanel", "Thumbnail size reset (Ctrl+Alt+9)") ==
              QStringLiteral("已重置缩略图大小 (Ctrl+Alt+9)"),
          "qm translates thumbnail size reset");
    check(embedded.translate(
              "CompareShortcuts",
              "Same as O when the compare canvas has focus; otherwise Tab moves focus") ==
              QStringLiteral("比较画布拥有焦点时与 O 相同；否则 Tab 移动焦点"),
          "qm translates compare canvas Tab note");
    check(embedded.translate("MainWindow", "Skip this version") == QStringLiteral("跳过此版本"),
          "qm translates Skip this version");
    check(embedded.translate("MainWindow", "Checking for updates…") ==
              QStringLiteral("正在检查更新…"),
          "qm translates the ellipsis update status");
    check(embedded.translate("RawSource", "RAW · Full decode") == QStringLiteral("RAW · 完整解码"),
          "qm translates the full RAW source badge");
    check(embedded.translate("UsmCompareDialog", "Cannot open %1 for USM sharpen comparison: %2") ==
              QStringLiteral("无法打开 %1 进行 USM 锐化对比：%2"),
          "qm translates the USM open failure");
    check(embedded.translate("PreferencesDialog", "RAW display") == QStringLiteral("RAW 显示"),
          "qm translates the RAW display preference");
    check(embedded.translate("MetadataModel", "Display source") == QStringLiteral("显示来源"),
          "qm translates Display source");

    Language::apply(LanguageMode::English);
    check(tr("MainWindow", "&File") == QStringLiteral("&File"),
          "switching back to English removes the translator");

    QSettings().clear();
    std::cout << (g_failures ? "FAILED" : "PASSED") << " (" << g_failures << " failure(s))\n";
    return g_failures ? 1 : 0;
}
