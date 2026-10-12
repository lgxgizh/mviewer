// M17: Sidecar persistence — per-image .xmp JSON sidecar files that mirror
// RatingStore data (rating, color label, pick, reject) alongside image files.
// Provides portability: moving a folder preserves its metadata.
#pragma once

#include <functional>
#include <string>

namespace mviewer::core
{

class RatingStore;

class SidecarStore
{
  public:
    static SidecarStore &instance();

    // Write sidecar for a single image path (reads data from RatingStore).
    bool writeSidecar(const std::string &imagePath);
    // Read sidecar and populate RatingStore if data differs.
    bool readSidecar(const std::string &imagePath);
    // Delete sidecar file.
    bool removeSidecar(const std::string &imagePath);

    // Bulk import: scan directory for .xmp files and merge into RatingStore.
    // Returns count of merged entries.
    int importDirectory(const std::string &dirPath, const std::function<bool()> &cancelled = {});
    // Bulk export: write sidecars for all known ratings in a directory.
    // Returns count of written sidecars.
    int exportDirectory(const std::string &dirPath);

    // Move the sidecar of oldImagePath so it belongs to newImagePath (file
    // rename/move/delete-to-trash and their undo). The rewritten sidecar
    // records the new image path. Returns true when a sidecar was moved.
    bool relocateSidecar(const std::string &oldImagePath, const std::string &newImagePath);

    // Get the sidecar file path for an image: the full file name plus
    // ".xmp" ("IMG_001.jpg" -> "IMG_001.jpg.xmp"), so images that differ
    // only by extension never share a sidecar.
    static std::string sidecarPath(const std::string &imagePath);
    // Pre-1.0.109 layout ("IMG_001.jpg" -> "IMG_001.xmp"). Empty when the
    // image has no extension (the legacy and current names would coincide).
    static std::string legacySidecarPath(const std::string &imagePath);
    // The legacy sidecar path when it exists and unambiguously belongs to
    // imagePath (its recorded "file" names this image, or no sibling image
    // shares the stem); empty otherwise.
    static std::string ownedLegacySidecar(const std::string &imagePath);

  private:
    SidecarStore() = default;
    std::string toJson(const std::string &imagePath);
    bool fromJson(const std::string &json, const std::string &imagePath);
    // Existing sidecar file for imagePath: current layout first, then an
    // owned legacy file. Empty when none.
    static std::string existingSidecar(const std::string &imagePath);
};

} // namespace mviewer::core
