#include "Language.h"

#include <QCoreApplication>
#include <QFile>
#include <QLocale>
#include <QSettings>
#include <QTranslator>

static void ensureLanguageResources()
{
    Q_INIT_RESOURCE(mviewer);
}

namespace mviewer::ui
{
namespace
{
LanguageMode g_mode = LanguageMode::System;

bool systemWantsChinese()
{
    const QString name = QLocale::system().name(); // e.g. "zh_CN"
    return name.startsWith(QStringLiteral("zh"), Qt::CaseInsensitive);
}
} // namespace

QTranslator &Language::translator()
{
    static QTranslator t;
    return t;
}

QString Language::languageModeKey(LanguageMode mode)
{
    switch (mode)
    {
    case LanguageMode::System:
        return QStringLiteral("system");
    case LanguageMode::ChineseSimplified:
        return QStringLiteral("zh_CN");
    case LanguageMode::English:
    default:
        return QStringLiteral("en");
    }
}

LanguageMode Language::languageModeFromKey(const QString &key)
{
    if (key == QStringLiteral("system"))
        return LanguageMode::System;
    if (key == QStringLiteral("zh_CN") || key == QStringLiteral("zh"))
        return LanguageMode::ChineseSimplified;
    return LanguageMode::English;
}

QString Language::languageModeName(LanguageMode mode)
{
    switch (mode)
    {
    case LanguageMode::System:
        return QCoreApplication::translate("Language", "System default");
    case LanguageMode::ChineseSimplified:
        return QStringLiteral("简体中文"); // i18n-allow: language endonym
    case LanguageMode::English:
    default:
        return QStringLiteral("English");
    }
}

LanguageMode Language::current()
{
    return g_mode;
}

bool Language::effectiveChinese()
{
    if (g_mode == LanguageMode::ChineseSimplified)
        return true;
    if (g_mode == LanguageMode::System)
        return systemWantsChinese();
    return false;
}

void Language::applyFromSettings()
{
    QSettings settings;
    const auto mode = languageModeFromKey(
        settings.value(QStringLiteral("uiLanguage"), QStringLiteral("system")).toString());
    apply(mode);
}

void Language::apply(LanguageMode mode)
{
    g_mode = mode;
    QSettings settings;
    settings.setValue(QStringLiteral("uiLanguage"), languageModeKey(mode));

    auto *app = QCoreApplication::instance();
    if (!app)
        return;

    app->removeTranslator(&translator());
    if (effectiveChinese())
    {
        // Prefer embedded resource; fall back to next-to-exe translations/.
        if (translator().load(QStringLiteral(":/i18n/mviewer_zh_CN.qm")) ||
            translator().load(QStringLiteral("mviewer_zh_CN"),
                              app->applicationDirPath() + QStringLiteral("/translations")))
        {
            app->installTranslator(&translator());
        }
    }
}

QString Language::localizedHelpHtml(const QString &name)
{
    ensureLanguageResources();
    const QString suffix = effectiveChinese() ? QStringLiteral("zh_CN") : QStringLiteral("en");
    for (const QString &lang : {suffix, QStringLiteral("en")})
    {
        QFile file(QStringLiteral(":/help/%1.%2.html").arg(name, lang));
        if (file.open(QIODevice::ReadOnly))
            return QString::fromUtf8(file.readAll());
    }
    return {};
}

} // namespace mviewer::ui
