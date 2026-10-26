#include "browseformats.h"

#include "core/image/ImageFormats.h"

#include <QCoreApplication>
#include <QFileInfo>
#include <QSettings>

namespace mviewer::browseformats
{
namespace
{

const char *const kSettingsKey = "browse/hiddenFormatGroups";
// Sentinel stored for "nothing hidden" so an empty choice is not mistaken for
// "never configured" (which falls back to the RAW-hidden default).
const char *const kNoneToken = "none";

QStringList fixedExtensions(const QString &id)
{
    if (id == QLatin1String("jpeg"))
        return {QStringLiteral("jpg"), QStringLiteral("jpeg"), QStringLiteral("jpe"),
                QStringLiteral("jfif")};
    if (id == QLatin1String("png"))
        return {QStringLiteral("png")};
    if (id == QLatin1String("webp"))
        return {QStringLiteral("webp")};
    if (id == QLatin1String("tiff"))
        return {QStringLiteral("tif"), QStringLiteral("tiff")};
    if (id == QLatin1String("heic"))
        return {QStringLiteral("heic"), QStringLiteral("heif"), QStringLiteral("hif"),
                QStringLiteral("avif")};
    if (id == QLatin1String("gif"))
        return {QStringLiteral("gif")};
    if (id == QLatin1String("bmp"))
        return {QStringLiteral("bmp"), QStringLiteral("dib")};
    return {};
}

bool isKnownGroup(const QString &id)
{
    for (const Group &g : groups())
        if (id == QLatin1String(g.id))
            return true;
    return false;
}

} // namespace

const std::vector<Group> &groups()
{
    static const std::vector<Group> kGroups = {
        {"jpeg", QT_TRANSLATE_NOOP("BrowseFormats", "JPEG")},
        {"png", QT_TRANSLATE_NOOP("BrowseFormats", "PNG")},
        {"webp", QT_TRANSLATE_NOOP("BrowseFormats", "WebP")},
        {"tiff", QT_TRANSLATE_NOOP("BrowseFormats", "TIFF")},
        {"heic", QT_TRANSLATE_NOOP("BrowseFormats", "HEIC / AVIF")},
        {"gif", QT_TRANSLATE_NOOP("BrowseFormats", "GIF")},
        {"bmp", QT_TRANSLATE_NOOP("BrowseFormats", "BMP")},
        {"raw", QT_TRANSLATE_NOOP("BrowseFormats", "RAW")},
        {"other", QT_TRANSLATE_NOOP("BrowseFormats", "Other formats")},
    };
    return kGroups;
}

QString groupLabel(const QString &id)
{
    for (const Group &g : groups())
        if (id == QLatin1String(g.id))
            return QCoreApplication::translate("BrowseFormats", g.label);
    return id;
}

QStringList groupExtensions(const QString &id)
{
    QStringList exts;
    for (const std::string &ext : mviewer::core::ImageFormats::supportedSuffixes())
    {
        const QString s = QString::fromStdString(ext);
        if (groupForSuffix(s) == id && !exts.contains(s))
            exts.append(s);
    }
    return exts;
}

QStringList availableGroupIds()
{
    QStringList ids;
    for (const Group &g : groups())
    {
        const QString id = QLatin1String(g.id);
        if (!groupExtensions(id).isEmpty())
            ids.append(id);
    }
    return ids;
}

QString groupForSuffix(const QString &suffixNoDot)
{
    QString s = suffixNoDot.trimmed().toLower();
    if (s.startsWith(QLatin1Char('.')))
        s.remove(0, 1);
    if (s.isEmpty())
        return QStringLiteral("other");
    if (mviewer::core::isRawExtension(s.toStdString()))
        return QStringLiteral("raw");
    for (const Group &g : groups())
    {
        const QString id = QLatin1String(g.id);
        if (fixedExtensions(id).contains(s))
            return id;
    }
    return QStringLiteral("other");
}

bool isPathHidden(const QString &path, const QStringList &hiddenGroups)
{
    if (hiddenGroups.isEmpty())
        return false;
    // Recursive-search display names carry a " [sub]" tail, so classify the
    // real path, never the caption.
    return hiddenGroups.contains(groupForSuffix(QFileInfo(path).suffix()));
}

QStringList defaultHiddenGroups()
{
    return {QStringLiteral("raw")};
}

QStringList loadHiddenGroups()
{
    QSettings settings;
    if (!settings.contains(QLatin1String(kSettingsKey)))
        return defaultHiddenGroups();
    QStringList out;
    const QStringList stored = settings.value(QLatin1String(kSettingsKey)).toStringList();
    for (const QString &id : stored)
        if (isKnownGroup(id) && !out.contains(id))
            out.append(id);
    return out;
}

void saveHiddenGroups(const QStringList &hiddenGroups)
{
    QSettings settings;
    QStringList clean;
    for (const QString &id : hiddenGroups)
        if (isKnownGroup(id) && !clean.contains(id))
            clean.append(id);
    if (clean.isEmpty())
        clean.append(QLatin1String(kNoneToken));
    settings.setValue(QLatin1String(kSettingsKey), clean);
}

} // namespace mviewer::browseformats
