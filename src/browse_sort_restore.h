#pragma once

// Persisted secondary/tertiary sort. SortRating and anything the pane combo
// does not offer fall back to SortName, and that combo entry is selected.
#include "thumbnailpanel.h"

#include <QComboBox>
#include <QSignalBlocker>

inline ThumbnailPanel::SortMode restoredBrowseSortMode(int stored, QComboBox *combo)
{
    const auto select = [combo](ThumbnailPanel::SortMode mode)
    {
        if (combo)
        {
            const int idx = combo->findData(static_cast<int>(mode));
            if (idx >= 0)
            {
                const QSignalBlocker blocker(combo);
                combo->setCurrentIndex(idx);
            }
        }
        return mode;
    };

    const bool offered = stored != static_cast<int>(ThumbnailPanel::SortRating) &&
                         stored >= static_cast<int>(ThumbnailPanel::SortName) &&
                         stored <= static_cast<int>(ThumbnailPanel::SortLens) &&
                         (!combo || combo->findData(stored) >= 0);
    if (!offered)
        return select(ThumbnailPanel::SortName);
    return select(static_cast<ThumbnailPanel::SortMode>(stored));
}
