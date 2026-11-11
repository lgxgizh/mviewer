#pragma once

// Test-only seams for ThumbnailPanel. Only tests and the .cpp files that define
// the out-of-line hooks include this header. Hooks read the same private state
// the old *ForTest members did (review P2-7).

#include "thumbnailpanel.h"

#include <QHash>
#include <QList>
#include <QPixmap>
#include <QRegularExpression>
#include <QString>
#include <QStringList>

#include <atomic>
#include <memory>
#include <unordered_set>

struct ThumbnailPanelTestAccess
{
    static std::shared_ptr<std::atomic<uint64_t>> scanGenToken(const ThumbnailPanel &panel)
    {
        return panel.m_scanGenToken;
    }

    static void seedDisplayThumb(ThumbnailPanel &panel, const QString &path, const QPixmap &pixmap);

    static int scaleCacheHits();

    static void insertProgressiveRows(ThumbnailPanel &panel,
                                      const QList<ThumbnailPanel::Entry> &batch)
    {
        panel.insertProgressiveRows(batch);
    }

    static void captureScrollAnchor(const ThumbnailPanel &panel, QString &anchorPath,
                                    int &anchorOffset)
    {
        panel.captureScrollAnchor(anchorPath, anchorOffset);
    }

    static QStringList browseContextMenuTitles(ThumbnailPanel &panel, const QStringList &selPaths);

    static QList<ThumbnailPanel::Entry> evaluateQuerySnapshot(
        const QList<ThumbnailPanel::Entry> &source, const QString &text, bool useFuzzy,
        const QRegularExpression &fuzzy, const mviewer::core::BrowseQuery &query,
        const std::unordered_set<std::string> &recents, const QHash<QString, QString> &metaIndex,
        const QHash<QString, int> &metaIso, const QHash<QString, QString> &metaCamera,
        const QHash<QString, QString> &metaLens)
    {
        mviewer::ui::GalleryFilterSnapshot snapshot;
        snapshot.text = text;
        snapshot.useFuzzy = useFuzzy;
        snapshot.fuzzy = fuzzy;
        snapshot.query = query;
        snapshot.recents = recents;
        snapshot.metaIndex = metaIndex;
        snapshot.metaIso = metaIso;
        snapshot.metaCamera = metaCamera;
        snapshot.metaLens = metaLens;
        return mviewer::ui::evaluateGalleryFilter(source, snapshot);
    }
};
