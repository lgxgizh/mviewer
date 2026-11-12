#pragma once
// Browse format visibility: which image format groups the gallery lists.
//
// Formats are grouped (JPEG / PNG / WebP / TIFF / HEIC / GIF / BMP / RAW /
// Sensor RAW / Gray / Other) for the multi-select file type dropdown
// ("typeFilterCombo"). The hidden set is persisted in QSettings
// ("browse/hiddenFormatGroups"). The default is {"raw", "gray"}: RAW because
// RAW+JPEG shoots otherwise show every frame twice, and Gray (headerless
// .gray/.grey, ADR 034) until the user checks it.
//
// "browse/hiddenFormatGroupsVersion" (int, current 2) migrates existing users.
// No groups key returns the default and writes nothing. When the groups key
// exists and the version is < 2 (missing counts as 1), "gray" is appended if
// absent, the list is written back like saveHiddenGroups (never empty: a
// stored {"none"} means nothing was hidden, so it becomes {"gray"}), and the
// version is set to 2. saveHiddenGroups() always writes version 2. Sensor RAW
// needs no migration: a saved list never contained "sensorraw", so it stays
// visible.
// Hidden entries never enter a ThumbnailPanel's source list (filtered at the UI-thread insertion
// points), so the progressive name-sorted scan fast path keeps working.

#include <QString>
#include <QStringList>

#include <algorithm>
#include <vector>

namespace mviewer::browseformats
{

struct Group
{
    const char *id;    // stable settings token, e.g. "raw"
    const char *label; // English source text; translate with groupLabel()
};

// All groups in menu order.
const std::vector<Group> &groups();

// Translated menu label for a group id.
QString groupLabel(const QString &id);

// Decodable extensions of a group: the group's suffixes intersected with
// ImageFormats::supportedSuffixes() (the live decoder registry), so the
// dropdown never advertises a format the app cannot open.
QStringList groupExtensions(const QString &id);

// Group ids (menu order) that have at least one decodable extension.
QStringList availableGroupIds();

// Group id for a suffix without the dot (case-insensitive). Unknown -> "other".
// RAW = camera RAW (LibRaw); plain .raw dumps are "sensorraw" (ADR 033).
// Headerless .gray/.grey are "gray" (ADR 034), before the sensorraw test.
QString groupForSuffix(const QString &suffixNoDot);

// True when `path`'s extension belongs to one of `hiddenGroups`.
bool isPathHidden(const QString &path, const QStringList &hiddenGroups);

// Persisted hidden set (QSettings "browse/hiddenFormatGroups"); default {"raw", "gray"}.
// Version key browse/hiddenFormatGroupsVersion (current 2): see the file comment.
QStringList defaultHiddenGroups();
QStringList loadHiddenGroups();
void saveHiddenGroups(const QStringList &hiddenGroups);

// Remove entries (anything with a `path` member) whose format is hidden.
template <typename EntryList>
void eraseHiddenEntries(EntryList &entries, const QStringList &hiddenGroups)
{
    if (hiddenGroups.isEmpty())
        return;
    entries.erase(std::remove_if(entries.begin(), entries.end(), [&hiddenGroups](const auto &e)
                                 { return isPathHidden(e.path, hiddenGroups); }),
                  entries.end());
}

// Move entries whose format is hidden from `entries` into `hiddenOut` (replaced),
// keeping the relative order of both lists.
template <typename EntryList>
void partitionHiddenEntries(EntryList &entries, const QStringList &hiddenGroups,
                            EntryList &hiddenOut)
{
    hiddenOut.clear();
    if (hiddenGroups.isEmpty())
        return;
    EntryList shown;
    for (const auto &e : entries)
    {
        if (isPathHidden(e.path, hiddenGroups))
            hiddenOut.append(e);
        else
            shown.append(e);
    }
    entries = std::move(shown);
}

} // namespace mviewer::browseformats
