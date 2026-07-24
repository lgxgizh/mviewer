#include "core/SettingsIO.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSettings>
#include <QVariant>

namespace mviewer::core
{
namespace
{

// Recursively dump a QSettings group into a QJsonObject.
QJsonObject dumpGroup(QSettings &s)
{
    QJsonObject obj;

    // Leaf keys in the current group.
    for (const QString &key : s.childKeys())
    {
        const QVariant v = s.value(key);
        // Store binary blobs (geometry, windowState, splitterState) as base64
        // so the JSON stays text-safe and portable across machines.
        if (v.typeId() == QMetaType::QByteArray)
        {
            obj[key] = QString::fromLatin1(v.toByteArray().toBase64());
            obj[key + QStringLiteral("__b64")] = true; // marker for import
        }
        else if (v.typeId() == QMetaType::QStringList)
        {
            QJsonArray arr;
            for (const QString &item : v.toStringList())
                arr.append(item);
            obj[key] = arr;
        }
        else if (v.canConvert<QString>())
        {
            obj[key] = v.toString();
        }
        else
        {
            // Fallback: store as string representation.
            obj[key] = v.toString();
        }
    }

    // Nested groups.
    for (const QString &group : s.childGroups())
    {
        s.beginGroup(group);
        obj[group] = dumpGroup(s);
        s.endGroup();
    }

    return obj;
}

// Recursively restore a QJsonObject into QSettings.
void restoreGroup(QSettings &s, const QJsonObject &obj)
{
    for (auto it = obj.begin(); it != obj.end(); ++it)
    {
        const QString key = it.key();
        // Skip base64 markers — they are metadata, not real keys.
        if (key.endsWith(QStringLiteral("__b64")))
            continue;

        if (it.value().isObject())
        {
            s.beginGroup(key);
            restoreGroup(s, it.value().toObject());
            s.endGroup();
        }
        else if (it.value().isArray())
        {
            QStringList list;
            for (const QJsonValue &v : it.value().toArray())
                if (v.isString())
                    list.append(v.toString());
            s.setValue(key, list);
        }
        else if (it.value().isString())
        {
            // Check for base64 marker sibling.
            const QString marker = key + QStringLiteral("__b64");
            if (obj.contains(marker) && obj[marker].toBool())
            {
                s.setValue(key, QByteArray::fromBase64(it.value().toString().toLatin1()));
            }
            else
            {
                s.setValue(key, it.value().toString());
            }
        }
        else if (it.value().isDouble())
        {
            // QJson stores ints as doubles; prefer int when exact.
            const double d = it.value().toDouble();
            if (d == static_cast<int>(d))
                s.setValue(key, static_cast<int>(d));
            else
                s.setValue(key, d);
        }
        else if (it.value().isBool())
        {
            s.setValue(key, it.value().toBool());
        }
    }
}

} // anonymous namespace

bool exportSettings(const std::string &path, std::string *errorOut)
{
    QSettings s;
    QJsonObject root = dumpGroup(s);
    root[QStringLiteral("settingsSchemaVersion")] = kSettingsSchemaVersion;
    root[QStringLiteral("schema")] = QStringLiteral("mviewer.settings");

    QFile f(QString::fromStdString(path));
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text))
    {
        if (errorOut)
            *errorOut = "cannot open file for writing: " + path;
        return false;
    }
    f.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    return true;
}

bool importSettings(const std::string &path, std::string *errorOut)
{
    QFile f(QString::fromStdString(path));
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
    {
        if (errorOut)
            *errorOut = "cannot open file for reading: " + path;
        return false;
    }
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
    if (!doc.isObject())
    {
        if (errorOut)
            *errorOut = "invalid JSON in settings file: " + path;
        return false;
    }
    const QJsonObject root = doc.object();
    if (root.value(QStringLiteral("schema")).toString() != QStringLiteral("mviewer.settings"))
    {
        if (errorOut)
            *errorOut = "not an MViewer settings file (missing schema marker)";
        return false;
    }

    QSettings s;
    restoreGroup(s, root);
    s.sync();
    return true;
}

void migrateSettingsIfNeeded()
{
    QSettings s;
    const int current = s.value(QStringLiteral("settingsSchemaVersion"), 0).toInt();
    if (current >= kSettingsSchemaVersion)
        return;

    // v0 → v1: no structural changes yet; just stamp the version so future
    // migrations have a baseline. Add real migration steps here as the
    // schema evolves (e.g. rename keys, convert value formats).
    s.setValue(QStringLiteral("settingsSchemaVersion"), kSettingsSchemaVersion);
    s.sync();
}

} // namespace mviewer::core
