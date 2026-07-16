#include "CropCommand.h"

#include "core/image/ImageBuffer.h"
#include "core/image/ImageFrame.h"

#include <utility>

CropCommand::CropCommand(std::shared_ptr<ImageFrame> frame, const mviewer::domain::Selection& region)
    : m_frame(std::move(frame)), m_region(region), m_impl(std::make_unique<Impl>())
{
}

bool CropCommand::canExecute() const
{
    return m_frame && !m_frame->pixels().isNull() && !m_region.isEmpty();
}

void CropCommand::execute()
{
    if (!canExecute())
        return;
    // Capture current pixels for undo before cropping.
    m_impl->saved = m_frame->pixels();
    m_impl->hasSaved = true;
    ImageData cropped = cropRegion(m_frame->pixels(), m_region);
    m_frame->setPixels(cropped);
}

void CropCommand::undo()
{
    if (!m_impl->hasSaved || !m_frame)
        return;
    m_frame->setPixels(m_impl->saved);
    m_impl->hasSaved = false;
}
