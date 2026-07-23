#pragma once

#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>
#include <QString>
#include <QStringList>

// ─── AppState ───────────────────────────────────────────────────────────────
// Lightweight, per-user application state persisted as JSON. Lives in the UI
// layer (uses Qt Core types) — NOT in core/ (keeps core Qt-free in headers).
//
// Holds the cross-session product state that makes MViewer feel like a tool you
// use daily rather than a demo:
//   * favorites      — user-pinned directories (no LRU eviction)
//   * lastDir        — directory open at shutdown (restored on launch)
//   * lastImage      — image open at shutdown (re-selected on launch)
//   * lastThumbScroll— thumbnail-grid scroll offset (browse position restored)
//   * analysisVisible/analysisPage — Analysis panel + active sub-page (P1-3)
//   * navSidebarVisible           — left nav tree visibility (P1-3)
//
// Recent-folders history is handled separately by core::RecentFiles (it has
// its own LRU + cap). AppState owns the *pinned* and *restored* state.
struct AppState
{
    QStringList favorites;
    QStringList recentFolders; // P0-1: LRU of recently opened folders (max 15)
    QStringList history;       // P0-1: recently opened images (max 50)
    QString lastDir;
    QString lastImage;
    int lastThumbScroll = 0;

    // P1-3: restore the Analysis workspace on next launch so reopening the
    // app lands the user exactly where they left off (not just the image).
    bool analysisVisible = true;   // Analysis panel shown at shutdown? (true = prior default)
    int analysisPage = 0;          // active Analysis sub-page (Histogram/RGB/...)
    bool navSidebarVisible = true; // left navigation tree visible at shutdown?

    // Load from the per-user config path. Missing/corrupt file => defaults
    // (empty favorites, empty lastDir). Never throws.
    static AppState load();

    // Persist to the per-user config path. Path is created if absent.
    // Returns false only on unrecoverable write error (still safe to ignore).
    bool save() const;

    // Convenience: add a favorite if not already present (dedup, append).
    void addFavorite(const QString &dir);
    bool removeFavorite(const QString &dir);
    bool isFavorite(const QString &dir) const;

    // P0-1: recent-folder LRU. Moves existing entry to front; caps at max.
    void addRecentFolder(const QString &dir);
    void clearRecentFolders();

    // P0-1: image history. Caps at max.
    void addHistory(const QString &imagePath);
    void clearHistory();
};
