#pragma once

// Atomic-then-in-place file replace used by rotate/flip. Qt-free header; the
// .cpp talks to QSaveFile. `utf8Path` is a UTF-8 encoded file path.

#include "core/image/ImageFileRotate.h"

#include <string>
#include <vector>

namespace mviewer::core
{

struct FileReplaceResult
{
    bool ok = false;
    ImageRotateError code = ImageRotateError::None;
    std::string error;
    bool usedInPlaceFallback = false;
};

FileReplaceResult replaceFileContents(const std::string &utf8Path,
                                      const std::vector<uint8_t> &bytes);

} // namespace mviewer::core
