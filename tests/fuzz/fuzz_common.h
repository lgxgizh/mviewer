// Shared helpers for the libFuzzer targets in tests/fuzz (review P1-5).
// The parsers under test take file paths, so each input is written to a
// per-process temp file with the extension that routes it to the parser.
#pragma once

#include <QCoreApplication>

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <string>

namespace mviewer::fuzz
{

// QImageReader-based paths want a QCoreApplication. The replay driver and
// libFuzzer's own main() both run without one, so create it on first use.
inline void ensureApp()
{
    if (QCoreApplication::instance())
        return;
    static int argc = 1;
    static char name[] = "mviewer_fuzz";
    static char *argv[] = {name, nullptr};
    static QCoreApplication app(argc, argv);
}

inline std::string writeTempInput(const uint8_t *data, size_t size, const char *extension)
{
    const auto dir = std::filesystem::temp_directory_path();
    const std::string name =
        "mviewer_fuzz_" + std::to_string(QCoreApplication::applicationPid()) + extension;
    const std::string path = (dir / name).string();
    FILE *f = std::fopen(path.c_str(), "wb");
    if (!f)
        return {};
    if (size > 0)
        std::fwrite(data, 1, size, f);
    std::fclose(f);
    return path;
}

} // namespace mviewer::fuzz
