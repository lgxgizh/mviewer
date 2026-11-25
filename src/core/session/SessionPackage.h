#pragma once

// Portable session package (report feature 11).
//
// A single shareable file that carries the compare session plus the images it
// references, so a review can be sent to a colleague without a shared drive.
// The container is a store-only ZIP (no compression): the payload is already
// encoded JPEG/PNG, so compression would cost CPU for almost no size, and a
// store-only writer keeps the implementation small and auditable.

#include <cstdint>
#include <string>
#include <vector>

namespace mviewer::core
{

struct SessionPackageEntry
{
    std::string name; // relative path inside the package
    std::vector<std::uint8_t> data;
};

/// Write a store-only ZIP. Returns false and fills `error` on I/O failure.
bool writeSessionPackage(const std::string &path, const std::vector<SessionPackageEntry> &entries,
                         std::string &error);

/// Read a store-only ZIP written by writeSessionPackage(). Only stored
/// (uncompressed), single-disk classic ZIP entries with UTF-8 names are
/// accepted. Input is bounded to 272 MiB before allocation; payload remains 128
/// MiB/entry, 256 MiB total. Names are portable flat names;
/// normalized/case-folded duplicates, non-regular entries and overlapping local
/// records are rejected. On any error, returns no partial entries. This API
/// does not extract to the filesystem.
std::vector<SessionPackageEntry> readSessionPackage(const std::string &path, std::string &error);

/// CRC-32 (IEEE 802.3), the checksum ZIP uses.
std::uint32_t sessionPackageCrc32(const std::uint8_t *data, size_t size);

} // namespace mviewer::core
