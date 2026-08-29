#pragma once

#include "FrameSequence.h"

#include <chrono>
#include <vector>

namespace mviewer::core
{

// Deterministic, Qt-free timeline for animated image playback. The viewer
// drives it from a short UI timer, while this class owns all elapsed-time and
// catch-up rules. Presentation time is anchored to steady_clock, so repeated
// timer callbacks cannot accumulate drift.
class FramePlaybackController
{
  public:
    using Clock = std::chrono::steady_clock;
    using TimePoint = Clock::time_point;
    using Milliseconds = std::chrono::milliseconds;

    struct Tick
    {
        bool due = false;
        bool skipped = false;
        bool looped = false;
        int frameIndex = 0;
        Milliseconds nextDelay{100};
    };

    void configure(const FrameSequenceInfo &sequence, const std::vector<FrameInfo> &frames = {});
    void setFrameInfo(const FrameInfo &frame);

    void start(TimePoint now = Clock::now());
    void pause(TimePoint now = Clock::now());
    void resume(TimePoint now = Clock::now());
    void restart(TimePoint now = Clock::now())
    {
        start(now);
    }

    bool playing() const
    {
        return m_playing;
    }
    int currentFrame() const
    {
        return m_currentFrame;
    }
    const FrameSequenceInfo &sequence() const
    {
        return m_sequence;
    }

    // Select a frame without resetting the image sequence. When playing, the
    // selected frame becomes the new timeline anchor; when paused it becomes
    // the frame that resume() will present first.
    void setCurrentFrame(int frameIndex, TimePoint now = Clock::now());

    Tick tick(TimePoint now = Clock::now());
    Milliseconds delayUntilNext(TimePoint now = Clock::now()) const;

  private:
    static constexpr int kDefaultFrameDelayMs = 100;
    static constexpr int kMinimumFrameDelayMs = 10;

    Milliseconds durationFor(int frameIndex) const;
    Milliseconds sequenceDuration() const;
    int frameAt(Milliseconds elapsed, bool *looped = nullptr) const;
    Milliseconds elapsedForFrame(int frameIndex) const;
    Milliseconds elapsedNow(TimePoint now) const;
    void anchorAt(Milliseconds elapsed, TimePoint now);

    FrameSequenceInfo m_sequence;
    std::vector<int> m_durations;
    int m_currentFrame = 0;
    bool m_playing = false;
    TimePoint m_startedAt = Clock::now();
    Milliseconds m_elapsedAtPause{0};
};

} // namespace mviewer::core
