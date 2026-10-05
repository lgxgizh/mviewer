#include "core/image/FramePlaybackController.h"

#include <algorithm>

namespace mviewer::core
{

void FramePlaybackController::configure(const FrameSequenceInfo &sequence,
                                         const std::vector<FrameInfo> &frames)
{
    m_sequence = sequence;
    m_sequence.frameCount = std::max(1, m_sequence.frameCount);
    m_currentFrame = std::clamp(m_sequence.defaultFrame, 0, m_sequence.frameCount - 1);
    m_durations.clear();
    m_durations.reserve(frames.size());
    for (const auto &frame : frames)
    {
        if (frame.index < 0)
            continue;
        if (frame.index >= static_cast<int>(m_durations.size()))
            m_durations.resize(static_cast<size_t>(frame.index + 1), kDefaultFrameDelayMs);
        m_durations[static_cast<size_t>(frame.index)] =
            std::max(kMinimumFrameDelayMs, frame.durationMs);
    }
    m_elapsedAtPause = elapsedForFrame(m_currentFrame);
    m_startedAt = Clock::now();
    m_playing = false;
    m_suspended = false;
}

void FramePlaybackController::setFrameInfo(const FrameInfo &frame)
{
    if (frame.index < 0)
        return;
    if (frame.index >= static_cast<int>(m_durations.size()))
        m_durations.resize(static_cast<size_t>(frame.index + 1), kDefaultFrameDelayMs);
    m_durations[static_cast<size_t>(frame.index)] =
        std::max(kMinimumFrameDelayMs, frame.durationMs);
}

void FramePlaybackController::start(TimePoint now)
{
    m_currentFrame = std::clamp(m_currentFrame, 0, m_sequence.frameCount - 1);
    m_elapsedAtPause = elapsedForFrame(m_currentFrame);
    m_startedAt = now - m_elapsedAtPause;
    m_suspended = false;
    m_playing = true;
}

void FramePlaybackController::pause(TimePoint now)
{
    if (!m_playing)
        return;
    if (!m_suspended)
        m_elapsedAtPause = elapsedNow(now);
    bool ignored = false;
    m_currentFrame = frameAt(m_elapsedAtPause, &ignored);
    m_playing = false;
    m_suspended = false;
}

void FramePlaybackController::suspend(TimePoint now)
{
    if (!m_playing || m_suspended)
        return;
    m_elapsedAtPause = elapsedNow(now);
    bool ignored = false;
    m_currentFrame = frameAt(m_elapsedAtPause, &ignored);
    m_suspended = true;
}

void FramePlaybackController::unsuspend(TimePoint now)
{
    if (!m_suspended)
        return;
    m_suspended = false;
    if (m_playing)
        m_startedAt = now - m_elapsedAtPause;
}

void FramePlaybackController::resume(TimePoint now)
{
    if (m_playing)
        return;
    m_startedAt = now - m_elapsedAtPause;
    m_playing = true;
}

void FramePlaybackController::setCurrentFrame(int frameIndex, TimePoint now)
{
    m_currentFrame = std::clamp(frameIndex, 0, m_sequence.frameCount - 1);
    m_elapsedAtPause = elapsedForFrame(m_currentFrame);
    if (m_playing)
        m_startedAt = now - m_elapsedAtPause;
}

FramePlaybackController::Milliseconds FramePlaybackController::durationFor(int frameIndex) const
{
    if (frameIndex >= 0 && frameIndex < static_cast<int>(m_durations.size()) &&
        m_durations[static_cast<size_t>(frameIndex)] > 0)
    {
        return Milliseconds(m_durations[static_cast<size_t>(frameIndex)]);
    }

    if (m_sequence.durationKnown && m_sequence.frameCount > 0 &&
        m_sequence.totalDurationMs > 0)
    {
        return Milliseconds(std::max(kMinimumFrameDelayMs,
                                     static_cast<int>(m_sequence.totalDurationMs /
                                                      m_sequence.frameCount)));
    }
    return Milliseconds(kDefaultFrameDelayMs);
}

FramePlaybackController::Milliseconds FramePlaybackController::sequenceDuration() const
{
    Milliseconds total{0};
    for (int i = 0; i < m_sequence.frameCount; ++i)
        total += durationFor(i);
    return std::max(Milliseconds(kMinimumFrameDelayMs), total);
}

int FramePlaybackController::frameAt(Milliseconds elapsed, bool *looped) const
{
    if (looped)
        *looped = false;
    if (m_sequence.frameCount <= 1)
        return 0;

    const auto total = sequenceDuration();
    if (elapsed.count() < 0)
        elapsed = Milliseconds(0);
    const auto cycle = elapsed / total;
    if (m_sequence.loopCount >= 0 && cycle > m_sequence.loopCount)
    {
        if (looped)
            *looped = true;
        return m_sequence.frameCount - 1;
    }
    if (looped && cycle > 0)
        *looped = true;

    const auto inCycle = elapsed % total;
    Milliseconds accumulated{0};
    for (int i = 0; i < m_sequence.frameCount; ++i)
    {
        accumulated += durationFor(i);
        if (inCycle < accumulated)
            return i;
    }
    return m_sequence.frameCount - 1;
}

FramePlaybackController::Milliseconds FramePlaybackController::elapsedForFrame(int frameIndex) const
{
    Milliseconds elapsed{0};
    for (int i = 0; i < std::clamp(frameIndex, 0, m_sequence.frameCount - 1); ++i)
        elapsed += durationFor(i);
    return elapsed;
}

FramePlaybackController::Milliseconds FramePlaybackController::elapsedNow(TimePoint now) const
{
    if (!m_playing || m_suspended)
        return m_elapsedAtPause;
    if (now <= m_startedAt)
        return Milliseconds(0);
    return std::chrono::duration_cast<Milliseconds>(now - m_startedAt);
}

void FramePlaybackController::anchorAt(Milliseconds elapsed, TimePoint now)
{
    m_elapsedAtPause = elapsed;
    m_startedAt = now - elapsed;
}

FramePlaybackController::Tick FramePlaybackController::tick(TimePoint now)
{
    Tick result;
    result.frameIndex = m_currentFrame;
    if (!m_playing || m_suspended || m_sequence.frameCount <= 1)
    {
        result.nextDelay = delayUntilNext(now);
        return result;
    }

    const auto previousElapsed = m_elapsedAtPause;
    const auto elapsed = elapsedNow(now);
    const auto total = sequenceDuration();
    // duration/duration is a scalar count of loops, not another duration.
    const auto cycles = total.count() > 0 ? elapsed.count() / total.count() : 0;
    const auto previousCycles = total.count() > 0 ? previousElapsed.count() / total.count() : 0;
    const int next = frameAt(elapsed, nullptr);
    const int previous = m_currentFrame;
    const int count = m_sequence.frameCount;
    result.finished = m_sequence.loopCount >= 0 && cycles > m_sequence.loopCount;
    result.looped = cycles > previousCycles;
    result.frameIndex = next;
    result.due = next != previous;
    int forward = next - previous;
    if (forward < 0)
        forward += count;
    // A normal wrap (last frame to the first) advances one frame. skipped
    // means the clock jumped over frames, not that a loop boundary was crossed.
    result.skipped = result.due && forward > 1;
    m_currentFrame = next;
    m_elapsedAtPause = elapsed;
    result.nextDelay = delayUntilNext(now);
    return result;
}

FramePlaybackController::Milliseconds FramePlaybackController::delayUntilNext(TimePoint now) const
{
    if (!m_playing || m_sequence.frameCount <= 1)
        return Milliseconds(kDefaultFrameDelayMs);

    const auto elapsed = elapsedNow(now);
    const auto total = sequenceDuration();
    const auto cycle = elapsed / total;
    if (m_sequence.loopCount >= 0 && cycle > m_sequence.loopCount)
        return Milliseconds(kDefaultFrameDelayMs);

    const auto inCycle = elapsed % total;
    Milliseconds accumulated{0};
    for (int i = 0; i < m_sequence.frameCount; ++i)
    {
        const auto duration = durationFor(i);
        if (inCycle < accumulated + duration)
        {
            const auto remaining = accumulated + duration - inCycle;
            return std::max(Milliseconds(kMinimumFrameDelayMs), remaining);
        }
        accumulated += duration;
    }
    return Milliseconds(kMinimumFrameDelayMs);
}

} // namespace mviewer::core
