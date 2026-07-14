#include "core/command/OpenDirectoryCommand.h"

#include <QKeySequence>

OpenDirectoryCommand::OpenDirectoryCommand(std::function<void()> onExecute)
    : m_onExecute(std::move(onExecute))
{
}

void OpenDirectoryCommand::execute()
{
    if (canExecute() && m_onExecute)
        m_onExecute();
}

bool OpenDirectoryCommand::canExecute() const
{
    return m_onExecute != nullptr;
}

std::vector<CommandShortcut> OpenDirectoryCommand::shortcuts() const
{
    return {{Qt::Key_O, Qt::ControlModifier}};
}
