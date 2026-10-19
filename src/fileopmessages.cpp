#include "fileopmessages.h"

#include <QCoreApplication>
#include <QString>

namespace mviewer::ui
{
namespace
{

// English phrases emitted by the core file-operation and undo/redo layer.
// Core strings stay English; the UI localizes each known phrase through the
// "FileOpMessages" translation context. Longer phrases precede shorter ones so
// a prefix is not rewritten inside a longer sentence that is translated whole.
constexpr const char *kFileOpPhrases[] = {
    QT_TRANSLATE_NOOP("FileOpMessages", "Some files remain in trash; use Undo to retry recovery."),
    QT_TRANSLATE_NOOP("FileOpMessages", "The source/destination state is retained for recovery."),
    QT_TRANSLATE_NOOP("FileOpMessages", "Source cleanup failed and destination rollback failed"),
    QT_TRANSLATE_NOOP("FileOpMessages", "Some files remain moved; use Undo to retry recovery."),
    QT_TRANSLATE_NOOP("FileOpMessages", "Rename destination must stay in the same directory: "),
    QT_TRANSLATE_NOOP("FileOpMessages", "Move cancelled and destination cleanup failed: "),
    QT_TRANSLATE_NOOP("FileOpMessages", "Atomic copy commit failed and cleanup failed"),
    QT_TRANSLATE_NOOP("FileOpMessages", "Source and destination are the same path: "),
    QT_TRANSLATE_NOOP("FileOpMessages", "Copy failed and temporary cleanup failed"),
    QT_TRANSLATE_NOOP("FileOpMessages", "All completed deletes were rolled back."),
    QT_TRANSLATE_NOOP("FileOpMessages", "Source file is missing or not regular: "),
    QT_TRANSLATE_NOOP("FileOpMessages", "All completed moves were rolled back."),
    QT_TRANSLATE_NOOP("FileOpMessages", "path collision or missing trash entry"),
    QT_TRANSLATE_NOOP("FileOpMessages", "Cannot create destination directory: "),
    QT_TRANSLATE_NOOP("FileOpMessages", "Source file is missing or not regular"),
    QT_TRANSLATE_NOOP("FileOpMessages", "path collision or missing moved file"),
    QT_TRANSLATE_NOOP("FileOpMessages", "Cannot inspect destination directory"),
    QT_TRANSLATE_NOOP("FileOpMessages", "Cannot create destination directory"),
    QT_TRANSLATE_NOOP("FileOpMessages", "Destination is not a directory: "),
    QT_TRANSLATE_NOOP("FileOpMessages", "Cannot create trash directory: "),
    QT_TRANSLATE_NOOP("FileOpMessages", "Trash path is not a directory: "),
    QT_TRANSLATE_NOOP("FileOpMessages", "Original name is now occupied: "),
    QT_TRANSLATE_NOOP("FileOpMessages", "Renamed file no longer exists: "),
    QT_TRANSLATE_NOOP("FileOpMessages", "Destination directory is empty."),
    QT_TRANSLATE_NOOP("FileOpMessages", "Source is not a regular file: "),
    QT_TRANSLATE_NOOP("FileOpMessages", "Cannot create trash directory"),
    QT_TRANSLATE_NOOP("FileOpMessages", "Trash path is not a directory"),
    QT_TRANSLATE_NOOP("FileOpMessages", "Destination already exists: "),
    QT_TRANSLATE_NOOP("FileOpMessages", "Source file does not exist: "),
    QT_TRANSLATE_NOOP("FileOpMessages", "Cannot inspect source size: "),
    QT_TRANSLATE_NOOP("FileOpMessages", "Cannot inspect destination"),
    QT_TRANSLATE_NOOP("FileOpMessages", "Cannot inspect source size"),
    QT_TRANSLATE_NOOP("FileOpMessages", "Atomic copy commit failed"),
    QT_TRANSLATE_NOOP("FileOpMessages", "Copy verification failed"),
    QT_TRANSLATE_NOOP("FileOpMessages", "Source file is missing"),
    QT_TRANSLATE_NOOP("FileOpMessages", "Source cleanup failed"),
    QT_TRANSLATE_NOOP("FileOpMessages", " Rollback failed for "),
    QT_TRANSLATE_NOOP("FileOpMessages", "Source does not exist"),
    QT_TRANSLATE_NOOP("FileOpMessages", "Undo rename failed: "),
    QT_TRANSLATE_NOOP("FileOpMessages", "Delete failed for "),
    QT_TRANSLATE_NOOP("FileOpMessages", "Command was lost."),
    QT_TRANSLATE_NOOP("FileOpMessages", " Undo failed for "),
    QT_TRANSLATE_NOOP("FileOpMessages", "Nothing to undo."),
    QT_TRANSLATE_NOOP("FileOpMessages", "Nothing to redo."),
    QT_TRANSLATE_NOOP("FileOpMessages", "Move failed for "),
    QT_TRANSLATE_NOOP("FileOpMessages", "Command was lost"),
    QT_TRANSLATE_NOOP("FileOpMessages", "Rename failed: "),
    QT_TRANSLATE_NOOP("FileOpMessages", "Copy cancelled."),
    QT_TRANSLATE_NOOP("FileOpMessages", "Move cancelled."),
    QT_TRANSLATE_NOOP("FileOpMessages", "Cannot inspect"),
    QT_TRANSLATE_NOOP("FileOpMessages", "path collision"),
    QT_TRANSLATE_NOOP("FileOpMessages", "unknown error"),
    QT_TRANSLATE_NOOP("FileOpMessages", "Copy failed"),
    QT_TRANSLATE_NOOP("FileOpMessages", "Move failed"),
    QT_TRANSLATE_NOOP("FileOpMessages", " file(s): "),
};

} // namespace

QString fileOpErrorText(const QString &message)
{
    QString text = message;
    for (const char *phrase : kFileOpPhrases)
    {
        const QString source = QString::fromUtf8(phrase);
        const QString translated = QCoreApplication::translate("FileOpMessages", phrase);
        if (translated != source && text.contains(source))
            text.replace(source, translated);
    }
    return text;
}

QString fileOpErrorText(const std::string &error)
{
    return fileOpErrorText(QString::fromStdString(error));
}

} // namespace mviewer::ui
