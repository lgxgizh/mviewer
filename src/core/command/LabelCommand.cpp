#include "LabelCommand.h"

#include "core/image/ImageFrame.h"

LabelCommand::LabelCommand(std::shared_ptr<ImageFrame> frame, std::string tag, Mode mode)
    : m_frame(std::move(frame))
    , m_tag(std::move(tag))
    , m_mode(mode)
{
}

std::string LabelCommand::description() const
{
    return (m_mode == Mode::Add ? "Add label '" : "Remove label '") + m_tag + "'";
}

void LabelCommand::execute()
{
    if (!m_frame)
        return;
    if (m_mode == Mode::Add)
        m_frame->addTag(m_tag);
    else
        m_frame->removeTag(m_tag);
}

void LabelCommand::undo()
{
    if (!m_frame)
        return;
    // Reverse: adding -> remove; removing -> add.
    if (m_mode == Mode::Add)
        m_frame->removeTag(m_tag);
    else
        m_frame->addTag(m_tag);
}
