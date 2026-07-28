#pragma once
// ThumbnailProvider: the single home for the "produce a thumbnail pixmap for a
// path at a given size" knowledge that used to live inside ThumbnailPanel's
// ThumbnailPipeline wiring (the decode lambda + result lambda). Extracting it:
//   * shrinks ThumbnailPanel's responsibility — the panel now only routes a
//     finished pixmap into its own per-panel ready map and manages lifecycle;
//   * centralizes the on-disk-cache lookup, decode, square-fit and cache-store
//     policy in one stateless, worker-thread-safe place.
// Lives in src/ (Qt allowed), not core/.
#include "core/image/ImageBuffer.h" // ImageData (complete type: decode() returns by value)

#include <QPixmap>
#include <QString>

// Stateless provider over the ThumbnailCache + Decoder singletons.
class ThumbnailProvider
{
  public:
    // Cache-or-decode `path` at `size`. Returns a null ImageData when neither the
    // on-disk cache nor the Decoder can produce pixels. Serves directly as the
    // body of ThumbnailPipeline's DecodeFn.
    static ImageData decode(const std::string &path, int size);

    // Fit `img` (already decoded) into a transparent size×size pixmap, centered
    // with KeepAspectRatio + SmoothTransformation. Null pixmap if `img` is null.
    static QPixmap squareFit(const ImageData &img, int size);

    // Persist `pm` to the on-disk thumbnail cache under `path`.
    static void cache(const QString &path, const QPixmap &pm);

    // One-shot pipeline result path: squareFit + disk cache. Returns the finished
    // pixmap (null iff the decode produced no pixels).
    static QPixmap produce(const std::string &path, const ImageData &img, int size);
};
