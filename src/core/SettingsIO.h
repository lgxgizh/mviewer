#pragma once

#include <string>

namespace mviewer::core
{

// Settings import/export helpers.
//
// Export dumps the entire QSettings tree (geometry, view modes, favorites,
// recent dirs, etc.) into a portable JSON file. Import restores it.
// Both functions are pure I/O — they do not touch the live UI; the caller
// is responsible for reloading state after a successful import.
//
// Header is Qt-free; the .cpp uses QSettings / QJsonDocument.

// Export all QSettings keys to `path` as indented JSON.
// Returns true on success; on failure `errorOut` receives a diagnostic.
bool exportSettings(const std::string &path, std::string *errorOut = nullptr);

// Import settings from a JSON file previously written by exportSettings.
// Existing keys not present in the file are left untouched.
// Returns true on success; on failure `errorOut` receives a diagnostic.
bool importSettings(const std::string &path, std::string *errorOut = nullptr);

// Current settings schema version. Bumped when the key layout changes in a
// way that requires migration. Stored as "settingsSchemaVersion" in QSettings.
constexpr int kSettingsSchemaVersion = 1;

// Run any pending migrations. Called once at startup after QCoreApplication
// is constructed. Safe to call repeatedly (idempotent).
void migrateSettingsIfNeeded();

} // namespace mviewer::core
