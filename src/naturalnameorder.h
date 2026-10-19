#pragma once

#include <QStringList>
#include <QStringView>

// Natural name order (img2 before img10). Text chunks are compared
// case-insensitively; digit runs by numeric value. Shared by the gallery's
// Name sort and the Compare workspace's default pane order.
int compareNaturalName(QStringView left, QStringView right);

// Order paths by file name with compareNaturalName(); ties (same name in
// different folders, or names differing only in case) fall back to the full
// path so the result is deterministic.
QStringList sortPathsByNaturalFileName(QStringList paths);
