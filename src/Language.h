#pragma once

#include <QString>

class QTranslator;

namespace mviewer::ui
{

// UI language preference. English is the source language in code (tr("...")).
// Chinese is loaded from translations/mviewer_zh_CN.qm when selected (or when
// System resolves to a zh* locale).
enum class LanguageMode
{
    System = 0,
    English = 1,
    ChineseSimplified = 2,
};

class Language
{
  public:
    // Read QSettings "uiLanguage" (default system) and install/remove the
    // zh_CN translator on qApp. Safe to call before any window is shown and
    // again after the user changes the preference (may require UI rebuild for
    // already-constructed widgets — callers should retranslate or restart).
    static void applyFromSettings();

    static void apply(LanguageMode mode);
    static LanguageMode current();

    static QString languageModeKey(LanguageMode mode);
    static LanguageMode languageModeFromKey(const QString &key);
    static QString languageModeName(LanguageMode mode);

    // Resolved effective language after System is expanded.
    static bool effectiveChinese();

  private:
    static QTranslator &translator();
};

} // namespace mviewer::ui
