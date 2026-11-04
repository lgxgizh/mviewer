#pragma once

// Systematic names for pixel formats the canonical FFmpeg-style table does not
// list (VU planar above 8 bit, NV61/NV42, LSB-aligned semi-planar, ...). The
// YUV parameter dialog builds a format from independent controls, so every
// valid combination needs a stable, parseable name for QSettings and the
// cache key. The grammar has no separators so a name also survives filename
// tokenization:
//
//   planar : ("yuv" | "yvu") ("420" | "422" | "444") "p" [depth ("le" | "be")] ["msb"]
//   gray   : "gray" [depth ("le" | "be")] ["msb"]
//   semi   : "nv" ("12" | "21" | "16" | "61" | "24" | "42") ["p" depth ("le" | "be")] ["lsb"]
//
// Planar samples above 8 bit default to LSB-aligned (FFmpeg yuv420p10le);
// semi-planar ones default to MSB-aligned (P010). Packed layouts only exist in
// the canonical table.

#include "core/image/yuv/YuvFormat.h"

#include <string>
#include <string_view>

namespace mviewer::core::yuv::detail
{

std::string genericPixFmtName(const Format &f);

// `lowered` must already be ASCII-lowercase. Writes only the pixel fields.
bool parseGenericPixFmtName(std::string_view lowered, Format &inout);

} // namespace mviewer::core::yuv::detail
