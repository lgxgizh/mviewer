#include "RotateCommand.h"

#include "core/image/ImageBuffer.h"
#include "core/image/ImageFrame.h"

#include <utility>

RotateCommand::RotateCommand(std::shared_ptr<ImageFrame> frame)
    : m_frame(std::move(frame))
    , m_impl(std::make_unique<Impl>())
{
}

bool RotateCommand::canExecute() const
{
    return m_frame && !m_frame->pixels().isNull();
}

void RotateCommand::execute()
{
    if (!canExecute())
        return;
    // Capture current pixels for undo before rotating.
    m_impl->saved = m_frame->pixels();
    m_impl->hasSaved = true;
    ImageData rotated = rotate90CW(m_frame->pixels());
    m_frame->setPixels(rotated);
}

void RotateCommand::undo()
{
    if (!m_impl->hasSaved || !m_frame)
        return;
    m_frame->setPixels(m_impl->saved);
    m_impl->hasSaved = false;
}
