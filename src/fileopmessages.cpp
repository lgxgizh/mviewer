#include "fileopmessages.h"

#include <QString>

namespace mviewer::ui
{
namespace
{

struct FileOpPhrase
{
    const char *english;
    const char *chinese;
};

// Longer phrases precede shorter ones so a prefix is not rewritten inside a
// longer sentence that is translated as a whole.
constexpr FileOpPhrase kFileOpPhrases[] = {
    {"Some files remain in trash; use Undo to retry recovery.",
     "部分文件仍在回收站中，请使用撤销重试恢复。"},
    {"The source/destination state is retained for recovery.", "源与目标状态已保留以便恢复。"},
    {"Source cleanup failed and destination rollback failed", "源文件清理失败，且目标回滚失败"},
    {"Some files remain moved; use Undo to retry recovery.",
     "部分文件仍停留在移动后的位置，请使用撤销重试恢复。"},
    {"Rename destination must stay in the same directory: ", "重命名目标必须位于同一目录："},
    {"Move cancelled and destination cleanup failed: ", "移动已取消，且目标清理失败："},
    {"Atomic copy commit failed and cleanup failed", "原子复制提交失败，且清理失败"},
    {"Source and destination are the same path: ", "源路径与目标路径相同："},
    {"Copy failed and temporary cleanup failed", "复制失败，且临时文件清理失败"},
    {"All completed deletes were rolled back.", "已完成的删除已全部回滚。"},
    {"Source file is missing or not regular: ", "源文件不存在或不是普通文件："},
    {"All completed moves were rolled back.", "已完成的移动已全部回滚。"},
    {"path collision or missing trash entry", "路径冲突或回收站条目丢失"},
    {"Cannot create destination directory: ", "无法创建目标目录："},
    {"Source file is missing or not regular", "源文件不存在或不是普通文件"},
    {"path collision or missing moved file", "路径冲突或移动后的文件丢失"},
    {"Cannot inspect destination directory", "无法检查目标目录"},
    {"Cannot create destination directory", "无法创建目标目录"},
    {"Destination is not a directory: ", "目标不是目录："},
    {"Cannot create trash directory: ", "无法创建回收目录："},
    {"Trash path is not a directory: ", "回收路径不是目录："},
    {"Original name is now occupied: ", "原文件名已被占用："},
    {"Renamed file no longer exists: ", "重命名后的文件已不存在："},
    {"Destination directory is empty.", "目标目录为空。"},
    {"Source is not a regular file: ", "源不是普通文件："},
    {"Cannot create trash directory", "无法创建回收目录"},
    {"Trash path is not a directory", "回收路径不是目录"},
    {"Destination already exists: ", "目标已存在："},
    {"Source file does not exist: ", "源文件不存在："},
    {"Cannot inspect source size: ", "无法检查源文件大小："},
    {"Cannot inspect destination", "无法检查目标"},
    {"Cannot inspect source size", "无法检查源文件大小"},
    {"Atomic copy commit failed", "原子复制提交失败"},
    {"Copy verification failed", "复制校验失败"},
    {"Source file is missing", "源文件不存在"},
    {"Source cleanup failed", "源文件清理失败"},
    {" Rollback failed for ", " 回滚失败："},
    {"Source does not exist", "源不存在"},
    {"Undo rename failed: ", "撤销重命名失败："},
    {"Delete failed for ", "删除失败："},
    {"Command was lost.", "命令已丢失。"},
    {" Undo failed for ", " 撤销失败："},
    {"Nothing to undo.", "没有可撤销的操作。"},
    {"Nothing to redo.", "没有可重做的操作。"},
    {"Move failed for ", "移动失败："},
    {"Command was lost", "命令已丢失"},
    {"Rename failed: ", "重命名失败："},
    {"Copy cancelled.", "复制已取消。"},
    {"Move cancelled.", "移动已取消。"},
    {"Cannot inspect", "无法检查"},
    {"path collision", "路径冲突"},
    {"unknown error", "未知错误"},
    {"Copy failed", "复制失败"},
    {"Move failed", "移动失败"},
    {" file(s): ", " 个文件："},
};

} // namespace

QString fileOpErrorZh(const QString &message)
{
    if (message.isEmpty())
        return message;
    QString text = message;
    for (const FileOpPhrase &row : kFileOpPhrases)
        text.replace(QString::fromLatin1(row.english), QString::fromUtf8(row.chinese));
    return text;
}

QString fileOpErrorZh(const std::string &error)
{
    return fileOpErrorZh(QString::fromStdString(error));
}

} // namespace mviewer::ui
