#pragma once

// File-backed image rotate for Browse/Viewer (±90° / 180°).
// Qt-free header; .cpp may use Qt (QImageReader / QSaveFile).

#include <cstdint>
#include <string>

namespace mviewer::core
{

enum class ImageRotateMethod : std::uint8_t
{
    None = 0,
    ExifOrientation, // reserved: JPEG orientation-tag rewrite
    PixelRewrite     // decode → exact 90°-step rotate → re-encode
};

struct ImageFileRotateResult
{
    bool ok = false;
    ImageRotateMethod method = ImageRotateMethod::None;
    int width = 0;
    int height = 0;
    std::string error; // stable English diagnostic for tests / logs
};

bool isWritableRotateFormat(const std::string &suffixOrFormat);

// Rotate the image file in place by a multiple of 90°. degreesCw is normalized
// modulo 360 (negative = CCW). Non-zero angles never silently no-op: unsupported
// formats and I/O failures return ok=false with a clear error.
ImageFileRotateResult rotateImageFile(const std::string &utf8Path, int degreesCw);

} // namespace mviewer::core
