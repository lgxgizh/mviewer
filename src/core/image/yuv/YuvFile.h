#pragma once

#include "core/image/yuv/YuvSource.h"
#include "domain/Image.h"

#include <cstdint>
#include <string>

// Opens a .yuv / .y4m file and exposes the resolved source plus a ReadAtFn.
// Qt-free. Never throws.

namespace mviewer::core::yuv
{

// Case-insensitive ".yuv" / ".y4m" or any extension in the YUV extension map
// (YuvExtensionMap; user-editable in Preferences > YUV). `.gray` / `.grey` are
// reserved for ADR 034 and are never YUV paths.
bool isYuvPath(const std::string &path);

// Key for YuvParamStore. Same normalization the repository uses for paths.
std::string yuvParamKey(const std::string &path);

struct OpenedYuvFile
{
    YuvSourceInfo info;
    ReadAtFn read;
    int64_t fileSize = 0;
};

// False when the file cannot be opened or sized. Otherwise true and out.info
// is resolved (out.info.known may be false). Never throws.
bool openYuvFile(const std::string &path, OpenedYuvFile &out);

// A ReadAtFn on its own stream for 'path' (empty when it cannot be opened), so
// several threads can read one file at once. Never throws.
ReadAtFn openYuvReader(const std::string &path);

// Fills source size, file size, names, bit depth, channels (1 gray else 3),
// format "YUV"/"Y4M", color space such as "BT.709 limited", frameCount clamped
// to [1, INT_MAX], currentFrame, sequenceKind ("pages" when frameCount > 1,
// otherwise "static"), and animated = false.
void fillYuvMetadata(const OpenedYuvFile &f, const std::string &path, int frameIndex,
                     mviewer::domain::ImageMetadata &meta);

// "|yuv=" + toString(saved format) when YuvParamStore has saved params for
// yuvParamKey(path); otherwise "". Store lookup only (no file I/O).
std::string yuvCacheKeySuffix(const std::string &path);

// Suffix empty -> key has no "|yuv=" marker. Otherwise key ends with suffix.
bool yuvCacheKeyAgrees(const std::string &key, const std::string &suffix);

} // namespace mviewer::core::yuv
