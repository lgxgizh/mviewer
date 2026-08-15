#pragma once

#include <QStandardPaths>
#include <QString>

namespace mviewer::runtime
{

// Returns an application-owned writable directory for a platform storage
// location. A missing/unwritable standard location falls back to an
// application-specific directory below the platform temp directory. An empty
// result means that neither location can be made writable; callers must then
// disable the optional tier or keep state in memory.
QString writableDirectory(QStandardPaths::StandardLocation location);

QString filePath(QStandardPaths::StandardLocation location, const QString &fileName);

// Make QSettings use the same writable, per-application AppConfig directory
// as the JSON state files instead of the platform registry or global config.
void configureSettings();

} // namespace mviewer::runtime
