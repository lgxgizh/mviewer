#include "core/image/Decoder.h"

#include <QFileInfo>
#include <QImage>
#include <QImageReader>
#include <QString>
#include <cmath>
#include <cstring>

namespace {

ImageData toImageData(const QImage &src) {
  if (src.isNull())
    return ImageData();
  const QImage img = src.convertToFormat(QImage::Format_RGB888);
  if (img.isNull())
    return ImageData();
  ImageData out = makeImageData(img.width(), img.height(), PixelFormat::RGB24);
  const int w = img.width();
  const int h = img.height();
  const size_t rowBytes = static_cast<size_t>(w) * 3;
  for (int y = 0; y < h; ++y) {
    const uchar *s = img.constScanLine(y);
    uint8_t *d = out.buffer.get() + static_cast<size_t>(y) * out.stride();
    std::memcpy(d, s, rowBytes);
  }
  return out;
}

} // namespace

std::vector<std::string> Decoder::supportedExtensions() {
  return {"*.jpg", "*.jpeg", "*.bmp", "*.png"};
}

ImageData Decoder::decodeFull(const std::string &path) {
  QImageReader reader(QString::fromStdString(path));
  reader.setAutoTransform(true); // 尊重 EXIF 方向
  return toImageData(reader.read());
}

ImageData Decoder::decodeScaled(const std::string &path, int maxEdge) {
  QImageReader reader(QString::fromStdString(path));
  reader.setAutoTransform(true);
  const QSize full = reader.size();
  if (!full.isValid() || full.isEmpty())
    return ImageData();
  if (full.width() <= maxEdge && full.height() <= maxEdge)
    return toImageData(reader.read()); // 本身已够小，直接解

  const double ratio =
      static_cast<double>(maxEdge) / std::max(full.width(), full.height());
  reader.setScaledSize(QSize(static_cast<int>(full.width() * ratio),
                             static_cast<int>(full.height() * ratio)));
  return toImageData(reader.read());
}
