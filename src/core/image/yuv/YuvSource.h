#pragma once

#include "core/image/ImageBuffer.h"
#include "core/image/yuv/Y4mHeader.h"
#include "core/image/yuv/YuvParamInference.h"
#include "core/image/yuv/YuvToRgb.h"
#include "core/image/yuv/YuvUnpack.h"

#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

// Resolves per-file YUV parameters and reads one frame into RGB24.
// All I/O goes through ReadAtFn. This module never opens files itself.

namespace mviewer::core::yuv
{

// Per-file parameters the user saved in the parameter dialog. Thread-safe.
// Keys are caller-normalized path strings.
class YuvParamStore
{
  public:
    static YuvParamStore &instance();

    void set(const std::string &key, const Format &f); // bumps revision()
    std::optional<Format> get(const std::string &key) const;
    void clear(const std::string &key); // bumps revision()
    void clearAll();
    uint64_t revision() const; // global change counter, starts at 0

  private:
    mutable std::mutex m_mutex;
    std::unordered_map<std::string, Format> m_map;
    uint64_t m_revision = 0;
};

enum class ParamSource : std::uint8_t
{
    None,
    Y4mHeader,
    Saved,
    Inferred
};

struct YuvSourceInfo
{
    bool known = false; // format fully resolved and valid; frames can be decoded
    bool isY4m = false;
    ParamSource source = ParamSource::None;
    Format format;       // best guess even when !known (UI pre-fills the dialog with it)
    Inference inference; // filled for raw files (what the file name / size suggested)
    int64_t frameBytes = 0;
    int64_t frameCount = 0;               // complete frames only
    int64_t trailingBytes = 0;            // raw: bytes after the last complete frame
    int64_t dataOffset = 0;               // raw: 0
    std::vector<int64_t> y4mFrameOffsets; // y4m: absolute pixel-data offset per frame
    int fpsNum = 0;
    int fpsDen = 0;  // y4m F tag, 0 when unknown
    std::string why; // human-readable English reason when !known
};

// Case-insensitive ".y4m" suffix.
bool isY4mFileName(std::string_view fileName);

// Resolution order (ADR 032): y4m header -> saved params -> inference
// (file name, folder, file size).
// y4m: read the first kMaxY4mHeaderBytes via 'read', parseY4mHeader, then
// indexY4mFrames. Saved params are ignored for y4m. Header parse failure ->
// known=false, why set.
// raw: if 'saved' has a value and isValid -> use it (source Saved); else
// inferYuvParams(fileName, folderName, fileSize) and known = inference.sizeKnown
// (source Inferred), format = inference.format.
// frameBytes from computeFrameLayout; frameCount/trailingBytes via
// computeFrameCount(fileSize, 0, 0, frameBytes). known=false if frameCount == 0
// (why: "file is smaller than one frame").
YuvSourceInfo resolveYuvSource(std::string_view fileName, std::string_view folderName,
                               int64_t fileSize, const ReadAtFn &read,
                               const std::optional<Format> &saved);

// Absolute byte offset of frame 'index' pixel data, or -1 when out of range / !known.
int64_t yuvFrameOffset(const YuvSourceInfo &info, int64_t index);

// Reads and unpacks frame 'index' (exactly frameBytes via 'read'; short read -> false).
bool readYuvFrame(const YuvSourceInfo &info, int64_t index, const ReadAtFn &read, YuvPlanes &out,
                  std::string *why = nullptr);

// Reads frame 'index' and renders it: identical pixels to readYuvFrame followed by
// renderYuvImage(planes, info.format, maxEdge, up). When the frame is scaled down,
// only samples that contribute are decoded (luma box on axes that shrink, nearest
// luma otherwise, chroma per 'up'). Empty ImageData (and *why on read failure).
// The upsample default is Bilinear and does not follow defaultChromaUpsample().
ImageData readYuvFrameImage(const YuvSourceInfo &info, int64_t index, const ReadAtFn &read,
                            int maxEdge, ChromaUpsample up = ChromaUpsample::Bilinear,
                            std::string *why = nullptr);

// Converts planes to an RGB24 ImageData. maxEdge > 0 and the longest edge larger
// than maxEdge -> output scaled down (aspect kept, longest edge == maxEdge, each
// dimension >= 1). Luma is a box average of the source samples covering each
// output pixel on axes that shrink, and one nearest sample on a 1:1 or upscaled
// axis. Chroma follows 'up'. maxEdge <= 0 -> full size. Empty on failure.
// The upsample default is Bilinear and does not follow defaultChromaUpsample().
ImageData renderYuvImage(const YuvPlanes &planes, const Format &f, int maxEdge = 0,
                         ChromaUpsample up = ChromaUpsample::Bilinear);

} // namespace mviewer::core::yuv
