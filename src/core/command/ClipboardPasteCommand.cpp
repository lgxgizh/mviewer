#include "core/command/ClipboardPasteCommand.h"

#include "core/filesystem/Utf8Path.h"

#include <filesystem>

namespace
{
namespace fs = std::filesystem;

std::string parentOf(const std::string &path)
{
    std::error_code ec;
    fs::path parent = fs::path(mviewer::core::pathFromUtf8(path)).parent_path();
    if (parent.empty())
        return {};
    return mviewer::core::pathToUtf8(parent.lexically_normal());
}

bool isInside(const std::string &path, const std::string &directory)
{
    if (directory.empty())
        return false;
    const std::string parent = parentOf(path);
    return !parent.empty() &&
           parent == mviewer::core::pathToUtf8(
                         fs::path(mviewer::core::pathFromUtf8(directory)).lexically_normal());
}
} // namespace

ClipboardPasteCommand::ClipboardPasteCommand(std::vector<std::string> created,
                                             std::string directory)
    : m_created(std::move(created)), m_directory(std::move(directory))
{
}

void ClipboardPasteCommand::execute()
{
    // The paste already ran on the UI thread; the stack only records history.
}

bool ClipboardPasteCommand::canUndo() const
{
    return !m_created.empty();
}

void ClipboardPasteCommand::undo()
{
    for (const std::string &path : m_created)
    {
        if (!isInside(path, m_directory))
            continue; // never delete outside the folder the paste wrote into
        // pathFromUtf8() is what makes this work on a Chinese / non-ASCII path:
        // constructing fs::path from a narrow UTF-8 string silently fails there.
        std::error_code ec;
        fs::remove(mviewer::core::pathFromUtf8(path), ec);
    }
}
