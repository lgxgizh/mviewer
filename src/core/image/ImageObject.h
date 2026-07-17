#pragma once

#include "ImageBuffer.h"
#include "ImageFrame.h"

#include <cstdint>
#include <string>

class ImageObject
{
  public:
    enum class DecodeState
    {
        Idle,
        Decoding,
        Decoded,
        Failed
    };
    enum class CacheState
    {
        None,
        Memory,
        Disk
    };

    ImageObject() = default;
    ImageObject(const std::string &path, const ImageData &image);

    bool isValid() const
    {
        return m_frame.isValid();
    }
    const std::string &path() const
    {
        return m_frame.metadata().filePath;
    }
    const ImageData &image() const
    {
        return m_frame.pixels();
    }
    int width() const
    {
        return m_frame.width();
    }
    int height() const
    {
        return m_frame.height();
    }

    int64_t fileSize() const
    {
        return static_cast<int64_t>(m_frame.metadata().fileSize);
    }
    // Return modification time as epoch seconds (Qt-free).
    int64_t modifiedEpochSec() const
    {
        return m_frame.metadata().modifiedEpochSec;
    }
    const std::string &hash() const
    {
        return m_frame.metadata().hash;
    }

    double luminanceMean();

    void rgbMeans(double &r, double &g, double &b);
    void rgbMeans(int &r, int &g, int &b);

    const int *histogram() const;
    DecodeState decodeState() const;
    void setDecodeState(DecodeState s);
    CacheState cacheState() const;
    void setCacheState(CacheState s);
    void computeHistogram();

    ImageFrame &frame()
    {
        return m_frame;
    }
    const ImageFrame &frame() const
    {
        return m_frame;
    }

  private:
    ImageFrame m_frame;
};

inline ImageObject::DecodeState ImageObject::decodeState() const
{
    return static_cast<DecodeState>(static_cast<uint8_t>(m_frame.decodeState()));
}
inline void ImageObject::setDecodeState(ImageObject::DecodeState s)
{
    m_frame.setDecodeState(static_cast<::DecodeState>(static_cast<uint8_t>(s)));
}
inline ImageObject::CacheState ImageObject::cacheState() const
{
    return static_cast<CacheState>(static_cast<uint8_t>(m_frame.cacheState()));
}
inline void ImageObject::setCacheState(ImageObject::CacheState s)
{
    m_frame.setCacheState(static_cast<::CacheState>(static_cast<uint8_t>(s)));
}
inline const int *ImageObject::histogram() const
{
    return m_frame.hasHistogram() ? m_frame.histogram().luminance.data() : nullptr;
}
