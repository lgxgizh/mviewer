#include "exportcommand.h"

#include "application/OpenDirectoryUseCase.h"
#include "exportdialog.h"

#include <QKeySequence>
#include <QSettings>
#include <QWidget>

ExportCommand::ExportCommand(QWidget* parent)
    : m_parent(parent)
{
}

bool ExportCommand::canExecute() const
{
    return true;
}

std::vector<CommandShortcut> ExportCommand::shortcuts() const
{
    return {{Qt::Key_S, Qt::ControlModifier}};
}

void ExportCommand::execute()
{
    QSettings settings;
    QString lastDir = settings.value("lastDir").toString();
    if (lastDir.isEmpty())
        return;

    auto result = OpenDirectoryUseCase::execute(lastDir.toStdString());
    QStringList images;
    for (const auto& p : result.imagePaths)
    {
        images.append(QString::fromStdString(p));
    }
    if (images.isEmpty())
        return;

    ExportDialog dlg(images, m_parent);
    dlg.exec();
}
