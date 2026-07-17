#include "core/command/DeleteCommand.h"

#include <QKeySequence>

DeleteCommand::DeleteCommand(std::function<void()> onExecute) : m_onExecute(std::move(onExecute))
{
}

void DeleteCommand::execute()
{
    if (canExecute() && m_onExecute)
        m_onExecute();
}

bool DeleteCommand::canExecute() const
{
    return m_onExecute != nullptr;
}

std::vector<CommandShortcut> DeleteCommand::shortcuts() const
{
    return {{Qt::Key_Delete, 0}};
}
