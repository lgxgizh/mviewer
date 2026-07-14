#include "core/compare/DifferenceEngine.h"
#include "core/image/QtConvert.h"
#include <QImage>
#include <algorithm>
#include <cmath>
#include <cstring>

ImageData DifferenceEngine::differenceMap(const ImageData &a,
                                          const ImageData &b) {
  if (a.isNull() || b.isNull())
    return ImageData();
  const QImage qa = mvcore::toQImage(a).convertToFormat(QImage::Format_RGB32);
  const QImage qb = mvcore::toQImage(b).convertToFormat(QImage::Format_RGB32);
  const int w = std::min(qa.width(), qb.width());
  const int h = std::min(qa.height(), qb.height());
  if (w <= 0 || h <= 0)
    return ImageData();
  QImage out(w, h, QImage::Format_Grayscale8);
  if (out.isNull())
    return ImageData();
  for (int y = 0; y < h; ++y) {
    const QRgb *la = reinterpret_cast<const QRgb *>(qa.constScanLine(y));
    const QRgb *lb = reinterpret_cast<const QRgb *>(qb.constScanLine(y));
    uchar *dst = out.scanLine(y);
    for (int x = 0; x < w; ++x) {
      const int dr = std::abs(static_cast<int>(qRed(la[x])) - qRed(lb[x]));
      const int dg = std::abs(static_cast<int>(qGreen(la[x])) - qGreen(lb[x]));
      const int db = std::abs(static_cast<int>(qBlue(la[x])) - qBlue(lb[x]));
      dst[x] = static_cast<uchar>(std::min(255, (dr + dg + db) / 3));
    }
  }
  return mvcore::fromQImage(out);
}

ImageData DifferenceEngine::heatMap(const ImageData &gray) {
  if (gray.isNull())
    return ImageData();
  const QImage g =
      mvcore::toQImage(gray).convertToFormat(QImage::Format_Grayscale8);
  const int w = g.width(), h = g.height();
  QImage out(w, h, QImage::Format_RGB32);
  if (out.isNull())
    return ImageData();
  for (int y = 0; y < h; ++y) {
    const uchar *src = g.constScanLine(y);
    QRgb *dst = reinterpret_cast<QRgb *>(out.scanLine(y));
    for (int x = 0; x < w; ++x) {
      const int v = src[x];
      // Blue (cold) → Green → Red (hot)
      int r, gg, b;
      if (v < 128) {
        r = 0;
        gg = v * 2;
        b = 255 - v * 2;
      } else {
        r = (v - 128) * 2;
        gg = 255 - (v - 128) * 2;
        b = 0;
      }
      dst[x] = qRgb(std::clamp(r, 0, 255), std::clamp(gg, 0, 255),
                    std::clamp(b, 0, 255));
    }
  }
  return mvcore::fromQImage(out);
}
