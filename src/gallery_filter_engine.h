#pragma once

// Value-only gallery filter evaluation. ThumbnailPanel builds one snapshot at
// the UI boundary; workers and tests call evaluateGalleryFilter without
// touching widgets, stores, or models.

#include "core/search/BrowseQuery.h"

#include <QDateTime>
#include <QHash>
#include <QList>
#include <QRegularExpression>
#include <QString>
#include <QStringView>

#include <string>
#include <unordered_set>

namespace mviewer::ui
{

struct GalleryEntry
{
    QString path;
    QString name;
    qint64 size = 0;
    int width = 0;
    int height = 0;
    QDateTime date;
    int frameCount = 1;
    bool animated = false;
};

QString galleryPathKey(const QString &path);

struct GalleryFilterSnapshot
{
    QString text;
    bool useFuzzy = false;
    QRegularExpression fuzzy;
    mviewer::core::BrowseQuery query;
    std::unordered_set<std::string> recents;
    QHash<QString, QString> metaIndex;
    QHash<QString, int> metaIso;
    QHash<QString, QString> metaCamera;
    QHash<QString, QString> metaLens;
};

bool galleryEntryMatches(const GalleryEntry &entry, const GalleryFilterSnapshot &snapshot);

QList<GalleryEntry> evaluateGalleryFilter(const QList<GalleryEntry> &source,
                                          const GalleryFilterSnapshot &snapshot);

} // namespace mviewer::ui

// Primary key follows `ascending`. Equal keys stay name-then-path, A→Z.
bool browseOrderedLess(int primary, QStringView nameA, QStringView nameB, const QString &pathA,
                       const QString &pathB, bool ascending);
int browsePrimaryCompare(mviewer::core::BrowseSortField field, const mviewer::ui::GalleryEntry &a,
                         const mviewer::ui::GalleryEntry &b,
                         const QHash<QString, QString> &metaCamera,
                         const QHash<QString, QString> &metaLens);
