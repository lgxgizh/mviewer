#pragma once

#include "core/image/yuv/YuvFormat.h"

#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// Guesses raw YUV parameters from the file name, the parent folder name and the
// file size (ADR 032 resolution steps 3-5). Pure std code; never touches the disk.

namespace mviewer::core::yuv
{

enum class SizeSource : std::uint8_t
{
    None,
    FileName,
    FolderName,
    FileSize
};

struct Inference
{
    // width/height are 0 when !sizeKnown. Defaults: yuv420p, BT.709, limited, left siting.
    Format format;
    bool sizeKnown = false;
    bool pixFmtKnown = false;
    bool matrixKnown = false;
    bool rangeKnown = false;
    SizeSource sizeSource = SizeSource::None;
    int64_t frameCount = 0; // complete frames; 0 when the size or file size is unknown
    int64_t trailingBytes = 0;
    // Every common size whose frame divides the file exactly (only filled when
    // the name did not give a size). One entry => used; several => ambiguous,
    // offered as choices by the parameter dialog.
    std::vector<std::pair<int, int>> sizeCandidates;

    bool complete() const
    {
        return sizeKnown;
    }
};

// fileName: base name with extension. folderName: immediate parent directory name
// (may be empty). fileSize: bytes, <= 0 when unknown.
Inference inferYuvParams(std::string_view fileName, std::string_view folderName, int64_t fileSize);

// First WxH (also 'X' or '*', each side 2..5 digits) anywhere in text, else a
// named size token (cif, 1080p, 4k, ...).
bool parseSizeToken(std::string_view text, int &w, int &h);

// Lowercased tokens split on _ - . space ( ) [ ] { } , + and '@'.
std::vector<std::string> tokenizeName(std::string_view name);

} // namespace mviewer::core::yuv
