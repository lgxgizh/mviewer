#pragma once

// User-configurable "file extension -> pixel format" table for headerless raw
// YUV files (ADR 032 follow-up). `.yuv` and `.y4m` are always recognised; the
// table adds more extensions (.nv12, .p010, ...) and may give any extension,
// `.yuv` included, a pixel format used when the file name has no explicit
// pix-fmt token. An empty pixFmt means "recognise the extension, infer the
// format as usual".
//
// Priority when resolving a raw file: y4m header > saved per-file params >
// pix-fmt token in the file name > extension mapping > folder-name hints >
// built-in default (yuv420p).
//
// The table is process-global and thread-safe; the UI loads it from QSettings
// at startup and replaces it from Preferences.

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace mviewer::core::yuv
{

struct ExtensionRule
{
    std::string ext;    // lowercase, no dot
    std::string pixFmt; // canonical pixel-format name, or empty for "auto"

    bool operator==(const ExtensionRule &) const = default;
};

// Built-in rules installed until the user edits the table.
std::vector<ExtensionRule> defaultExtensionRules();

// "NV12" / ".nv12" -> "nv12". Returns empty when the text is not a valid
// extension (1-16 chars of [a-z0-9_]) or names y4m (always header-driven).
std::string normalizeExtension(std::string_view text);

// Replaces the table. Invalid rows (bad extension, unknown pix-fmt) are dropped;
// a later duplicate extension replaces an earlier one; pix-fmt names are
// canonicalised. Returns the rules actually installed.
std::vector<ExtensionRule> setExtensionRules(const std::vector<ExtensionRule> &rules);
std::vector<ExtensionRule> extensionRules();

// Every extension treated as raw YUV (always contains "yuv" and "y4m").
std::vector<std::string> yuvExtensions();
bool isYuvExtension(std::string_view ext);

// Mapped pixel format for an extension; nullopt when unmapped or "auto".
std::optional<std::string> mappedPixFmt(std::string_view ext);

// Stable text that changes whenever the table changes (cache-key input).
std::string extensionRulesFingerprint();

// Serialised form used by QSettings: one "ext=pixfmt" entry per rule.
std::string formatExtensionRule(const ExtensionRule &rule);
std::optional<ExtensionRule> parseExtensionRule(std::string_view text);

} // namespace mviewer::core::yuv
