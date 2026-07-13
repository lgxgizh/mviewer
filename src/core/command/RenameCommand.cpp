#include "core/command/RenameCommand.h"

#include <QKeySequence>

RenameCommand::RenameCommand(std::function<void()> onExecute)
    : m_onExecute(std::move(onExecute)) {}

void RenameCommand::execute() {
    if (canExecute() && m_onExecute) m_onExecute();
}

bool RenameCommand::canExecute() const {
    return m_onExecute != nullptr;
}

std::vector<CommandShortcut> RenameCommand::shortcuts() const {
    return {{Qt::Key_F2, 0}};
}
