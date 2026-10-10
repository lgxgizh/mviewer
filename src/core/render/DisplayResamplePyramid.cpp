#include "core/render/DisplayResampleDetail.h"

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <utility>

namespace mviewer::core::resample_detail
{
namespace
{

int avg2(int a, int b)
{
    return (a + b + 1) >> 1;
}

int channelOffset(const RgbView &src, int channel)
{
    if (src.gray)
        return 0;
    if (channel == 1)
        return src.green;
    if (channel == 2)
        return src.blue;
    return src.red;
}

int sampleAt(const RgbView &src, int x, int y, int channel)
{
    x = clampIndex(x, src.width);
    y = clampIndex(y, src.height);
    const uint8_t *row = src.data + static_cast<size_t>(y) * static_cast<size_t>(src.stride) *
                                        static_cast<size_t>(src.channels);
    const uint8_t *pixel = row + static_cast<size_t>(x) * static_cast<size_t>(src.channels);
    return pixel[channelOffset(src, channel)];
}

int boxBodyWidth(const RgbView &src, int dstW, bool halfX)
{
    if (halfX && (src.width & 1) != 0)
        return dstW - 1;
    return dstW;
}

int boxBodyHeight(const RgbView &src, int dstH, bool halfY)
{
    if (halfY && (src.height & 1) != 0)
        return dstH - 1;
    return dstH;
}

void axisSpan(int srcCount, int dstIndex, bool half, int &lo, int &hi)
{
    if (!half)
    {
        lo = dstIndex;
        hi = dstIndex;
        return;
    }
    const int dstCount = srcCount >> 1;
    if ((srcCount & 1) != 0 && dstIndex == dstCount - 1)
    {
        lo = srcCount - 3;
        if (lo < 0)
            lo = 0;
        hi = srcCount - 1;
        return;
    }
    lo = dstIndex * 2;
    hi = lo + 1;
}

int averageSpan(const RgbView &src, int x0, int x1, int y0, int y1, int channel)
{
    int sum = 0;
    int n = 0;
    for (int y = y0; y <= y1; ++y)
    {
        for (int x = x0; x <= x1; ++x)
        {
            sum += sampleAt(src, x, y, channel);
            ++n;
        }
    }
    if (n <= 0)
        return 0;
    return (sum + n / 2) / n;
}

void writeEdgePixel(const RgbView &src, uint8_t *dst, int dstW, int x, int y, bool halfX,
                    bool halfY)
{
    int x0 = 0;
    int x1 = 0;
    int y0 = 0;
    int y1 = 0;
    axisSpan(src.width, x, halfX, x0, x1);
    axisSpan(src.height, y, halfY, y0, y1);
    uint8_t *pixel =
        dst + (static_cast<size_t>(y) * static_cast<size_t>(dstW) + static_cast<size_t>(x)) * 3u;
    pixel[0] = static_cast<uint8_t>(averageSpan(src, x0, x1, y0, y1, 0));
    pixel[1] = static_cast<uint8_t>(averageSpan(src, x0, x1, y0, y1, 1));
    pixel[2] = static_cast<uint8_t>(averageSpan(src, x0, x1, y0, y1, 2));
}

bool needsHalve(int src, int dst)
{
    return dst > 0 && src >= 2 && static_cast<long long>(src) >= static_cast<long long>(dst) * 2LL;
}

void adoptRgb(RgbView &view, const std::shared_ptr<std::vector<uint8_t>> &rgb, int width,
              int height)
{
    view.data = rgb->data();
    view.width = width;
    view.height = height;
    view.stride = width;
    view.channels = 3;
    view.red = 0;
    view.green = 1;
    view.blue = 2;
    view.gray = false;
    view.keep = rgb;
}

std::shared_ptr<std::vector<uint8_t>> renderBox(const RgbView &src, int dstW, int dstH, bool halfX,
                                                bool halfY, const std::atomic<bool> *cancel)
{
    const size_t bytes = static_cast<size_t>(dstW) * static_cast<size_t>(dstH) * 3u;
    auto rgb = std::make_shared<std::vector<uint8_t>>(bytes);
    if (!boxReduce(src, rgb->data(), dstW, dstH, halfX, halfY, cancel))
        return {};
    return rgb;
}

struct CacheLevel
{
    int shiftX = 1;
    int shiftY = 1;
    int width = 0;
    int height = 0;
    std::shared_ptr<std::vector<uint8_t>> rgb;
};

struct CacheEntry
{
    std::weak_ptr<std::vector<uint8_t>> source;
    int originX = 0;
    int originY = 0;
    int srcW = 0;
    int srcH = 0;
    std::vector<CacheLevel> levels;
    std::chrono::steady_clock::time_point used;
};

struct Flight
{
    const std::vector<uint8_t> *key = nullptr;
    int originX = 0;
    int originY = 0;
    int srcW = 0;
    int srcH = 0;
    int shiftX = 1;
    int shiftY = 1;
    int width = 0;
    int height = 0;
    bool done = false;
    std::shared_ptr<std::vector<uint8_t>> rgb;
    std::condition_variable cv;
};

struct Claim
{
    std::shared_ptr<std::vector<uint8_t>> rgb;
    std::shared_ptr<Flight> flight;
    int width = 0;
    int height = 0;
    uint64_t generation = 0;
    bool missed = false;
};

class PyramidCache
{
  public:
    static PyramidCache &instance()
    {
        static PyramidCache cache;
        return cache;
    }

    Claim claim(const std::shared_ptr<std::vector<uint8_t>> &buffer, const SourceRect &rect,
                int shiftX, int shiftY)
    {
        std::unique_lock<std::mutex> lock(mu_);
        dropExpired();
        Claim out;
        out.generation = generation_;
        if (const CacheLevel *level = findLevel(buffer, rect, shiftX, shiftY))
        {
            out.rgb = level->rgb;
            out.width = level->width;
            out.height = level->height;
            return out;
        }
        for (const std::shared_ptr<Flight> &flight : flights_)
        {
            if (!sameFlight(*flight, buffer.get(), rect, shiftX, shiftY))
                continue;
            std::shared_ptr<Flight> waiting = flight;
            waiting->cv.wait(lock, [&waiting]() { return waiting->done; });
            out.rgb = waiting->rgb;
            out.width = waiting->width;
            out.height = waiting->height;
            out.missed = out.rgb == nullptr;
            return out;
        }
        auto flight = std::make_shared<Flight>();
        flight->key = buffer.get();
        flight->originX = rect.x;
        flight->originY = rect.y;
        flight->srcW = rect.width;
        flight->srcH = rect.height;
        flight->shiftX = shiftX;
        flight->shiftY = shiftY;
        flights_.push_back(flight);
        out.flight = std::move(flight);
        return out;
    }

    void publish(const Claim &ticket, const std::shared_ptr<std::vector<uint8_t>> &rgb, int width,
                 int height, const std::shared_ptr<std::vector<uint8_t>> &buffer,
                 const SourceRect &rect, int shiftX, int shiftY)
    {
        std::lock_guard<std::mutex> lock(mu_);
        if (ticket.generation == generation_ && buffer && rgb)
            insertLevel(buffer, rect, shiftX, shiftY, width, height, rgb);
        finish(ticket.flight, rgb, width, height);
    }

    void abort(const Claim &ticket)
    {
        std::lock_guard<std::mutex> lock(mu_);
        finish(ticket.flight, {}, 0, 0);
    }

    void release() noexcept
    {
        std::lock_guard<std::mutex> lock(mu_);
        entries_.clear();
        bytes_ = 0;
        ++generation_;
    }

  private:
    static bool sameFlight(const Flight &flight, const std::vector<uint8_t> *key,
                           const SourceRect &rect, int shiftX, int shiftY)
    {
        return flight.key == key && flight.originX == rect.x && flight.originY == rect.y &&
               flight.srcW == rect.width && flight.srcH == rect.height && flight.shiftX == shiftX &&
               flight.shiftY == shiftY;
    }

    bool sameSource(const CacheEntry &entry, const std::shared_ptr<std::vector<uint8_t>> &buffer,
                    const SourceRect &rect) const
    {
        const std::shared_ptr<std::vector<uint8_t>> locked = entry.source.lock();
        if (!locked || locked.get() != buffer.get())
            return false;
        return entry.originX == rect.x && entry.originY == rect.y && entry.srcW == rect.width &&
               entry.srcH == rect.height;
    }

    size_t entryBytes(const CacheEntry &entry) const
    {
        size_t bytes = 0;
        for (const CacheLevel &level : entry.levels)
        {
            if (level.rgb)
                bytes += level.rgb->size();
        }
        return bytes;
    }

    void dropExpired()
    {
        std::vector<CacheEntry> kept;
        kept.reserve(entries_.size());
        size_t bytes = 0;
        for (CacheEntry &entry : entries_)
        {
            if (entry.source.expired())
                continue;
            bytes += entryBytes(entry);
            kept.push_back(std::move(entry));
        }
        entries_ = std::move(kept);
        bytes_ = bytes;
    }

    CacheLevel *findLevel(const std::shared_ptr<std::vector<uint8_t>> &buffer,
                          const SourceRect &rect, int shiftX, int shiftY)
    {
        for (CacheEntry &entry : entries_)
        {
            if (!sameSource(entry, buffer, rect))
                continue;
            entry.used = std::chrono::steady_clock::now();
            for (CacheLevel &level : entry.levels)
            {
                if (level.shiftX == shiftX && level.shiftY == shiftY)
                    return &level;
            }
        }
        return nullptr;
    }

    void insertLevel(const std::shared_ptr<std::vector<uint8_t>> &buffer, const SourceRect &rect,
                     int shiftX, int shiftY, int width, int height,
                     const std::shared_ptr<std::vector<uint8_t>> &rgb)
    {
        CacheEntry *entry = nullptr;
        for (CacheEntry &candidate : entries_)
        {
            if (!sameSource(candidate, buffer, rect))
                continue;
            entry = &candidate;
            break;
        }
        if (entry == nullptr)
        {
            CacheEntry created;
            created.source = buffer;
            created.originX = rect.x;
            created.originY = rect.y;
            created.srcW = rect.width;
            created.srcH = rect.height;
            entries_.push_back(std::move(created));
            entry = &entries_.back();
        }
        for (const CacheLevel &level : entry->levels)
        {
            if (level.shiftX == shiftX && level.shiftY == shiftY)
                return;
        }
        CacheLevel level;
        level.shiftX = shiftX;
        level.shiftY = shiftY;
        level.width = width;
        level.height = height;
        level.rgb = rgb;
        entry->levels.push_back(std::move(level));
        entry->used = std::chrono::steady_clock::now();
        bytes_ += rgb->size();
        evict();
    }

    void evict()
    {
        while (bytes_ > kBudget && entries_.size() > 1)
        {
            auto oldest = entries_.begin();
            for (auto it = entries_.begin(); it != entries_.end(); ++it)
            {
                if (it->used < oldest->used)
                    oldest = it;
            }
            bytes_ -= entryBytes(*oldest);
            entries_.erase(oldest);
        }
    }

    void finish(const std::shared_ptr<Flight> &flight,
                const std::shared_ptr<std::vector<uint8_t>> &rgb, int width, int height)
    {
        if (!flight)
            return;
        flight->rgb = rgb;
        flight->width = width;
        flight->height = height;
        flight->done = true;
        flight->cv.notify_all();
        flights_.erase(std::remove(flights_.begin(), flights_.end(), flight), flights_.end());
    }

    std::mutex mu_;
    std::vector<CacheEntry> entries_;
    std::vector<std::shared_ptr<Flight>> flights_;
    size_t bytes_ = 0;
    uint64_t generation_ = 1;
    static constexpr size_t kBudget = 96u * 1024u * 1024u;
};

bool buildUncached(RgbView &view, int dstW, int dstH, bool halfX, bool halfY,
                   const std::atomic<bool> *cancel)
{
    const std::shared_ptr<std::vector<uint8_t>> rgb =
        renderBox(view, dstW, dstH, halfX, halfY, cancel);
    if (!rgb)
        return false;
    adoptRgb(view, rgb, dstW, dstH);
    return true;
}

bool stepDown(RgbView &view, const SampleLayout &original, const SourceRect &rect, int dstW,
              int dstH, int shiftX, int shiftY, bool halfX, bool halfY,
              const std::atomic<bool> *cancel)
{
    if (!original.buffer)
        return buildUncached(view, dstW, dstH, halfX, halfY, cancel);
    PyramidCache &cache = PyramidCache::instance();
    for (int attempt = 0; attempt < 2; ++attempt)
    {
        const Claim claim = cache.claim(original.buffer, rect, shiftX, shiftY);
        if (claim.rgb)
        {
            adoptRgb(view, claim.rgb, claim.width, claim.height);
            return true;
        }
        if (!claim.flight)
            continue;
        const std::shared_ptr<std::vector<uint8_t>> rgb =
            renderBox(view, dstW, dstH, halfX, halfY, cancel);
        if (!rgb)
        {
            cache.abort(claim);
            return false;
        }
        cache.publish(claim, rgb, dstW, dstH, original.buffer, rect, shiftX, shiftY);
        adoptRgb(view, rgb, dstW, dstH);
        return true;
    }
    return buildUncached(view, dstW, dstH, halfX, halfY, cancel);
}

} // namespace

void boxWriteBodyScalar(const RgbView &src, uint8_t *dst, int dstW, int dstH, bool halfX,
                        bool halfY, int y0, int y1)
{
    (void)dstH;
    const int bodyW = boxBodyWidth(src, dstW, halfX);
    for (int y = y0; y < y1; ++y)
    {
        const int sy0 = halfY ? y * 2 : y;
        const int sy1 = halfY ? sy0 + 1 : sy0;
        uint8_t *out = dst + static_cast<size_t>(y) * static_cast<size_t>(dstW) * 3u;
        for (int x = 0; x < bodyW; ++x)
        {
            const int sx0 = halfX ? x * 2 : x;
            const int sx1 = halfX ? sx0 + 1 : sx0;
            for (int channel = 0; channel < 3; ++channel)
            {
                const int value =
                    avg2(avg2(sampleAt(src, sx0, sy0, channel), sampleAt(src, sx1, sy0, channel)),
                         avg2(sampleAt(src, sx0, sy1, channel), sampleAt(src, sx1, sy1, channel)));
                out[static_cast<size_t>(x) * 3u + static_cast<size_t>(channel)] =
                    static_cast<uint8_t>(value);
            }
        }
    }
}

void boxWriteEdges(const RgbView &src, uint8_t *dst, int dstW, int dstH, bool halfX, bool halfY)
{
    const bool oddX = halfX && (src.width & 1) != 0;
    const bool oddY = halfY && (src.height & 1) != 0;
    if (oddX)
    {
        const int x = dstW - 1;
        for (int y = 0; y < dstH; ++y)
            writeEdgePixel(src, dst, dstW, x, y, halfX, halfY);
    }
    if (!oddY)
        return;
    const int y = dstH - 1;
    const int xEnd = oddX ? dstW - 1 : dstW;
    for (int x = 0; x < xEnd; ++x)
        writeEdgePixel(src, dst, dstW, x, y, halfX, halfY);
}

bool boxReduce(const RgbView &src, uint8_t *dst, int dstW, int dstH, bool halfX, bool halfY,
               const std::atomic<bool> *cancel)
{
    if (src.data == nullptr || dst == nullptr || dstW <= 0 || dstH <= 0)
        return false;
    const int bodyH = boxBodyHeight(src, dstH, halfY);
    const bool allowFast = viewIsPackedRgb(src) && activeIsa() != Isa::Scalar;
    parallelForRows(bodyH, 32, cancel,
                    [&](int y0, int y1)
                    {
                        if (resampleCancelled(cancel))
                            return;
                        const bool ran = allowFast && boxWriteBodyFast(src, dst, dstW, dstH, halfX,
                                                                       halfY, y0, y1);
                        if (!ran)
                            boxWriteBodyScalar(src, dst, dstW, dstH, halfX, halfY, y0, y1);
                    });
    if (resampleCancelled(cancel))
        return false;
    boxWriteEdges(src, dst, dstW, dstH, halfX, halfY);
    return true;
}

bool reduceByBox(RgbView &view, const SampleLayout &original, const SourceRect &rect, int dstW,
                 int dstH, const std::atomic<bool> *cancel)
{
    if (view.data == nullptr || dstW <= 0 || dstH <= 0)
        return false;
    int shiftX = 1;
    int shiftY = 1;
    while (needsHalve(view.width, dstW) || needsHalve(view.height, dstH))
    {
        if (resampleCancelled(cancel))
            return false;
        const bool halfX = needsHalve(view.width, dstW);
        const bool halfY = needsHalve(view.height, dstH);
        const int nextW = halfX ? (view.width >> 1) : view.width;
        const int nextH = halfY ? (view.height >> 1) : view.height;
        const int nextShiftX = halfX ? shiftX * 2 : shiftX;
        const int nextShiftY = halfY ? shiftY * 2 : shiftY;
        if (!stepDown(view, original, rect, nextW, nextH, nextShiftX, nextShiftY, halfX, halfY,
                      cancel))
            return false;
        shiftX = nextShiftX;
        shiftY = nextShiftY;
    }
    return !resampleCancelled(cancel);
}

void releasePyramidCache() noexcept
{
    PyramidCache::instance().release();
}

} // namespace mviewer::core::resample_detail
