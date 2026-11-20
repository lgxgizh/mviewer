// ImageViewer state accessors. These used to be inline in imageviewer.h; they
// live in their own TU so the header and the viewer's core TU both stay inside
// their line budgets (ADR-014).
#include "imageviewer.h"

int ImageViewer::frameIndex() const
{
    return m_frame ? m_frame->frameIndex() : m_frameIndex;
}

int ImageViewer::frameCount() const
{
    return std::max(1, m_sequence.frameCount);
}

bool ImageViewer::isPlaying() const
{
    return m_playback.playing();
}

bool ImageViewer::isMultiFrame() const
{
    return frameCount() > 1;
}

QSize ImageViewer::transitionSnapshotSize() const
{
    return m_transitionImage.size();
}

std::shared_ptr<ImageFrame> ImageViewer::frame() const
{
    return m_frame;
}

const mviewer::core::FrameSequenceInfo &ImageViewer::sequenceInfo() const
{
    return m_sequence;
}

int ImageViewer::displayRasterWarmCount() const
{
    return static_cast<int>(m_displayRasterWarm.size());
}

qint64 ImageViewer::displayRasterWarmBytes() const
{
    return m_displayRasterWarmBytes;
}

uint64_t ImageViewer::displayRasterWarmHits() const
{
    return m_displayRasterWarmHits;
}
