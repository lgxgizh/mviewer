#include "yuvparamsettings.h"

#include "core/image/yuv/YuvSource.h"

#include <QSettings>
#include <QString>
#include <QStringList>

namespace
{

using mviewer::core::yuv::ChromaUpsample;

QString chromaUpsampleKey()
{
    return QString::fromLatin1(mviewer::ui::kYuvChromaUpsampleSettingsKey);
}

ChromaUpsample upsampleFromText(const QString &text)
{
    if (text == QLatin1String("nearest"))
    {
        return ChromaUpsample::Nearest;
    }
    return ChromaUpsample::Bilinear;
}

QString textFromUpsample(ChromaUpsample up)
{
    if (up == ChromaUpsample::Nearest)
    {
        return QStringLiteral("nearest");
    }
    return QStringLiteral("bilinear");
}

} // namespace

namespace mviewer::ui
{
namespace
{

QString settingsKey()
{
    return QString::fromLatin1(kYuvParamsSettingsKey);
}

QStringList readLines()
{
    QSettings settings;
    return settings.value(settingsKey()).toStringList();
}

void writeLines(const QStringList &lines)
{
    QSettings settings;
    settings.setValue(settingsKey(), lines);
}

QStringList withoutKey(const QStringList &lines, const QString &keyText)
{
    QStringList kept;
    kept.reserve(lines.size());
    for (const QString &line : lines)
    {
        const int tab = line.indexOf(QLatin1Char('\t'));
        if (tab > 0 && line.left(tab) == keyText)
        {
            continue;
        }
        kept.push_back(line);
    }
    return kept;
}

void trimOldest(QStringList &lines)
{
    while (lines.size() > kMaxSavedYuvParams)
    {
        lines.removeFirst();
    }
}

} // namespace

int loadYuvParamSettings()
{
    int loaded = 0;
    const QStringList lines = readLines();
    for (const QString &line : lines)
    {
        const int tab = line.indexOf(QLatin1Char('\t'));
        if (tab <= 0)
        {
            continue;
        }
        const std::string key = line.left(tab).toStdString();
        if (key.empty())
        {
            continue;
        }
        const auto parsed = mviewer::core::yuv::parseFormatString(line.mid(tab + 1).toStdString());
        if (!parsed || !mviewer::core::yuv::isValid(*parsed))
        {
            continue;
        }
        mviewer::core::yuv::YuvParamStore::instance().set(key, *parsed);
        ++loaded;
    }
    return loaded;
}

void saveYuvParam(const std::string &key, const mviewer::core::yuv::Format &f)
{
    mviewer::core::yuv::YuvParamStore::instance().set(key, f);
    const QString keyText = QString::fromStdString(key);
    QStringList lines = withoutKey(readLines(), keyText);
    const QString formatText = QString::fromStdString(mviewer::core::yuv::toString(f));
    lines.push_back(keyText + QLatin1Char('\t') + formatText);
    trimOldest(lines);
    writeLines(lines);
}

void clearYuvParam(const std::string &key)
{
    mviewer::core::yuv::YuvParamStore::instance().clear(key);
    const QString keyText = QString::fromStdString(key);
    writeLines(withoutKey(readLines(), keyText));
}

mviewer::core::yuv::ChromaUpsample loadYuvChromaUpsampleSetting()
{
    QSettings settings;
    const QString text = settings.value(chromaUpsampleKey(), QStringLiteral("bilinear")).toString();
    const ChromaUpsample up = upsampleFromText(text);
    mviewer::core::yuv::setDefaultChromaUpsample(up);
    return up;
}

void saveYuvChromaUpsampleSetting(mviewer::core::yuv::ChromaUpsample up)
{
    mviewer::core::yuv::setDefaultChromaUpsample(up);
    QSettings settings;
    settings.setValue(chromaUpsampleKey(), textFromUpsample(up));
}

std::vector<mviewer::core::yuv::ExtensionRule> loadYuvExtensionMapSettings()
{
    namespace yuv = mviewer::core::yuv;
    QSettings settings;
    const QString key = QString::fromLatin1(kYuvExtensionMapSettingsKey);
    if (!settings.contains(key))
    {
        return yuv::setExtensionRules(yuv::defaultExtensionRules());
    }
    std::vector<yuv::ExtensionRule> rules;
    const QStringList lines = settings.value(key).toStringList();
    for (const QString &line : lines)
    {
        if (const auto rule = yuv::parseExtensionRule(line.toStdString()))
        {
            rules.push_back(*rule);
        }
    }
    return yuv::setExtensionRules(rules);
}

std::vector<mviewer::core::yuv::ExtensionRule>
saveYuvExtensionMap(const std::vector<mviewer::core::yuv::ExtensionRule> &rules)
{
    namespace yuv = mviewer::core::yuv;
    const std::vector<yuv::ExtensionRule> installed = yuv::setExtensionRules(rules);
    QStringList lines;
    for (const yuv::ExtensionRule &rule : installed)
    {
        lines.push_back(QString::fromStdString(yuv::formatExtensionRule(rule)));
    }
    QSettings settings;
    settings.setValue(QString::fromLatin1(kYuvExtensionMapSettingsKey), lines);
    return installed;
}

} // namespace mviewer::ui
