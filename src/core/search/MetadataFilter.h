#pragma once
// M25: field-scoped metadata matching for the Browse filters.
//
// The gallery's Camera / Lens / ISO filters must match their OWN metadata
// fields — a camera filter must never match text that only appears in the
// lens field, and vice versa. Before this helper the filters matched against
// one concatenated searchable blob, so cross-field hits leaked through.
//
// Qt-free (pure std): lives in core and is unit-tested without a display.

#include "core/image/RawMetadata.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <string>

namespace mviewer::core::metadata
{

// Case-insensitive substring match (needle is typically already lowercased
// by the caller; the haystack is folded here so callers may pass raw fields).
inline bool containsFold(const std::string &haystack, const std::string &needle)
{
    if (needle.empty())
        return true;
    if (haystack.size() < needle.size())
        return false;
    return std::search(haystack.begin(), haystack.end(), needle.begin(), needle.end(),
                       [](unsigned char a, unsigned char b)
                       { return std::tolower(a) == std::tolower(b); }) != haystack.end();
}

// Lowercased combined camera identity ("make model") for one file.
inline std::string cameraText(const RawMetadata &raw)
{
    std::string out = raw.make;
    if (!raw.model.empty())
    {
        if (!out.empty())
            out += " ";
        out += raw.model;
    }
    return out;
}

// True when `text` appears (case-insensitively) in the camera fields.
inline bool matchesCamera(const RawMetadata &raw, const std::string &text)
{
    return containsFold(cameraText(raw), text);
}

// True when `text` appears (case-insensitively) in the lens field.
inline bool matchesLens(const RawMetadata &raw, const std::string &text)
{
    return containsFold(raw.lens, text);
}

// ISO value the pipeline should trust for filters/details (the parser fills
// both `iso` and `isoSpeed` from the same tag; `iso` is the canonical one).
inline int isoOf(const RawMetadata &raw)
{
    return static_cast<int>(raw.iso != 0 ? raw.iso : raw.isoSpeed);
}

// Exact ISO match (0 = any).
inline bool matchesIso(const RawMetadata &raw, int iso)
{
    return iso <= 0 || isoOf(raw) == iso;
}

} // namespace mviewer::core::metadata
