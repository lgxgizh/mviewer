#pragma once
// ThumbnailProvider: the single home for the "produce a thumbnail for a path
// at a given size" knowledge — the on-disk-cache lookup, decode, square-fit
// and cache-store policy in one stateless, worker-thread-safe place.
//
// M25 threading contract:
//   * EVERYTHING here may run on a worker thread: cache reads/writes (QImage
//     payload + PNG encode on the worker), decode, square-fit. No QPixmap is
//     ever created off the GUI thread.
//   * The GUI thread only converts the finished ImageData to a QPixmap once
//     per cell (cheap) and repaints the model.
#include "core/image/ImageBuffer.h" // ImageData (complete type: decode() returns by value)

#include <QImage>
#include <QString>

class ThumbnailProvider
{
  public:
    // One-shot worker path: disk-cache lookup → decode → square-fit →
    // disk-cache store. Returns the final square thumbnail (null ImageData
    // when neither the cache nor the decoder can produce pixels). Serves
    // directly as the body of ThumbnailPipeline's DecodeFn.
    static ImageData produce(const std::string &path, int size);

    static void invalidateSource(const std::string &path);

    // Fit `q` into a transparent size×size image, centered with
    // KeepAspectRatio + SmoothTransformation. Null image if `q` is null.
    static QImage squareFitImage(const QImage &q, int size);
};
