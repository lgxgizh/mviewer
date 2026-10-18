//
// Copyright (c) 2026 mviewer project. All rights reserved.
// SPDX-License-Identifier: MIT
//
// Keeps per-file metadata attached to a file when it changes path: star
// rating, color label, pick/reject, recents entry, tags and the
// .mviewer.json sidecar. Called by the rename/move/delete commands on success and on
// undo/rollback so metadata never stays behind on the old path.
#pragma once

#include <string>

namespace mviewer::core
{

void relocateFileMetadata(const std::string &oldPath, const std::string &newPath);

} // namespace mviewer::core
