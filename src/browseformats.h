#pragma once
// Browse format visibility: which image format groups the gallery lists.
//
// Formats are grouped (JPEG / PNG / WebP / TIFF / HEIC / GIF / BMP / RAW /
// Other) for the multi-select file type dropdown ("typeFilterCombo"). The hidden set is persisted
// in QSettings; RAW is hidden by default because RAW+JPEG shoots otherwise show every frame twice.
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
QString groupForSuffix(const QString &suffixNoDot);

// True when `path`'s extension belongs to one of `hiddenGroups`.
bool isPathHidden(const QString &path, const QStringList &hiddenGroups);

// Persisted hidden set (QSettings "browse/hiddenFormatGroups"); default {"raw"}.
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

} // namespace mviewer::browseformats
