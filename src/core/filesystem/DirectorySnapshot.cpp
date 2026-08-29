#include "core/filesystem/DirectorySnapshot.h"

#include "core/image/ImageFormats.h"
#include "core/filesystem/Utf8Path.h"

#include <QDir>
#include <QFileInfo>
#include <algorithm>
#include <cctype>
#include <filesystem>
#include <map>
#include <set>
#include <unordered_map>

namespace mviewer::core
{

namespace
{

std::string lower(std::string value)
{
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return value;
}

std::string normalizedPath(const QString &path)
{
    return QDir::cleanPath(QDir::fromNativeSeparators(path)).toUtf8().toStdString();
}

std::string identityFor(const QFileInfo &info)
{
    // QFileInfo's canonical path is not a file identity: it changes on rename.
    // Qt exposes no portable file-id API, so use a conservative stat-like
    // fingerprint as the cross-platform fallback. A unique fingerprint is
    // enough to recognize ordinary Explorer renames; ambiguous collisions are
    // deliberately left as add+remove by the diff code.
    return std::to_string(info.size()) + ":" +
           std::to_string(info.lastModified().toMSecsSinceEpoch()) + ":" +
           lower(info.suffix().toUtf8().toStdString());
}

DirectoryEntry makeEntry(const QFileInfo &info)
{
    DirectoryEntry entry;
    entry.path = normalizedPath(info.absoluteFilePath());
    entry.filename = info.fileName().toUtf8().toStdString();
    entry.identity = identityFor(info);
    entry.extension = lower(info.suffix().toUtf8().toStdString());
    entry.size = info.size() < 0 ? 0 : static_cast<uint64_t>(info.size());
    entry.modifiedEpochMs = info.lastModified().toMSecsSinceEpoch();
    return entry;
}

using EntryMap = std::unordered_map<std::string, DirectoryEntry>;

EntryMap byPath(const std::vector<DirectoryEntry> &entries)
{
    EntryMap result;
    result.reserve(entries.size());
    for (const DirectoryEntry &entry : entries)
        result.emplace(entry.path, entry);
    return result;
}

std::string fingerprint(const DirectoryEntry &entry)
{
    return entry.identity.empty()
               ? (std::to_string(entry.size) + ":" + std::to_string(entry.modifiedEpochMs) + ":" +
                  entry.extension)
               : entry.identity;
}

void diffEntries(const std::vector<DirectoryEntry> &beforeEntries,
                 const std::vector<DirectoryEntry> &afterEntries, std::vector<DirectoryEntry> &added,
                 std::vector<DirectoryEntry> &removed, std::vector<DirectoryEntry> &modified,
                 std::vector<DirectoryRename> &renamed)
{
    const EntryMap before = byPath(beforeEntries);
    const EntryMap after = byPath(afterEntries);
    std::set<std::string> removedPaths;
    std::set<std::string> addedPaths;

    for (const auto &[path, oldEntry] : before)
    {
        const auto it = after.find(path);
        if (it == after.end())
            removedPaths.insert(path);
        else if (!(oldEntry == it->second))
            modified.push_back(it->second);
    }
    for (const auto &[path, newEntry] : after)
        if (!before.contains(path))
            addedPaths.insert(path);

    // Build unique fingerprint buckets on each side.  A matching fingerprint
    // is a rename only when it occurs exactly once on both sides.
    std::unordered_map<std::string, std::vector<std::string>> oldByFingerprint;
    std::unordered_map<std::string, std::vector<std::string>> newByFingerprint;
    for (const std::string &path : removedPaths)
        oldByFingerprint[fingerprint(before.at(path))].push_back(path);
    for (const std::string &path : addedPaths)
        newByFingerprint[fingerprint(after.at(path))].push_back(path);

    std::set<std::string> renamedOld;
    std::set<std::string> renamedNew;
    for (const auto &[key, oldPaths] : oldByFingerprint)
    {
        const auto it = newByFingerprint.find(key);
        if (oldPaths.size() != 1 || it == newByFingerprint.end() || it->second.size() != 1)
            continue;
        renamed.push_back({before.at(oldPaths.front()), after.at(it->second.front())});
        renamedOld.insert(oldPaths.front());
        renamedNew.insert(it->second.front());
    }

    for (const std::string &path : removedPaths)
        if (!renamedOld.contains(path))
            removed.push_back(before.at(path));
    for (const std::string &path : addedPaths)
        if (!renamedNew.contains(path))
            added.push_back(after.at(path));

    auto byPathOrder = [](const DirectoryEntry &a, const DirectoryEntry &b)
    { return a.path < b.path; };
    std::sort(added.begin(), added.end(), byPathOrder);
    std::sort(removed.begin(), removed.end(), byPathOrder);
    std::sort(modified.begin(), modified.end(), byPathOrder);
    std::sort(renamed.begin(), renamed.end(),
              [](const DirectoryRename &a, const DirectoryRename &b)
              { return a.before.path < b.before.path; });
}

} // namespace

DirectorySnapshot snapshotDirectory(const std::string &path, uint64_t generation)
{
    DirectorySnapshot snapshot;
    snapshot.path = path;
    snapshot.generation = generation;
    const QDir directory(QString::fromUtf8(path.data(), static_cast<int>(path.size())));
    if (!directory.exists())
        return snapshot;

    snapshot.available = true;
    const QFileInfoList files = directory.entryInfoList(QDir::Files | QDir::Readable | QDir::NoDotAndDotDot,
                                                        QDir::Name | QDir::IgnoreCase);
    snapshot.entries.reserve(files.size());
    for (const QFileInfo &info : files)
    {
        const QString suffix = info.suffix().toLower();
        DirectoryEntry entry = makeEntry(info);
        if (ImageFormats::isSupportedSuffix(suffix.toStdString()))
            snapshot.entries.push_back(std::move(entry));
        else if (suffix == QStringLiteral("xmp"))
            snapshot.sidecars.push_back(std::move(entry));
    }
    return snapshot;
}

DirectoryDelta diffDirectorySnapshots(const DirectorySnapshot &before,
                                      const DirectorySnapshot &after)
{
    DirectoryDelta delta;
    delta.path = after.path;
    delta.fromGeneration = before.generation;
    delta.toGeneration = after.generation;
    if (!after.available)
    {
        if (!before.available)
            return delta;
        delta.directoryUnavailable = true;
        delta.removed = before.entries;
        delta.sidecarRemoved = before.sidecars;
        return delta;
    }

    delta.directoryRecovered = !before.available;

    diffEntries(before.entries, after.entries, delta.added, delta.removed, delta.modified,
                delta.renamed);
    std::vector<DirectoryRename> unusedRenames;
    diffEntries(before.sidecars, after.sidecars, delta.sidecarAdded, delta.sidecarRemoved,
                delta.sidecarModified, unusedRenames);
    return delta;
}

} // namespace mviewer::core
