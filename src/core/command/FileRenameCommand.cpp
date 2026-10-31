#include "FileRenameCommand.h"

#include "core/FileMetadataRelocation.h"
#include "core/filesystem/Utf8Path.h"

#include <filesystem>
#include <string>

namespace fs = std::filesystem;

namespace
{

bool asciiEqualFold(const std::string &left, const std::string &right)
{
    if (left.size() != right.size())
        return false;
    for (size_t i = 0; i < left.size(); ++i)
    {
        unsigned char a = static_cast<unsigned char>(left[i]);
        unsigned char b = static_cast<unsigned char>(right[i]);
        if (a >= 'A' && a <= 'Z')
            a = static_cast<unsigned char>(a - 'A' + 'a');
        if (b >= 'A' && b <= 'Z')
            b = static_cast<unsigned char>(b - 'A' + 'a');
        if (a != b)
            return false;
    }
    return true;
}

// True when `directory` could be listed. `foundExact` is the target spelling.
bool directoryHasExactName(const fs::path &directory, const std::string &name, bool &foundExact)
{
    foundExact = false;
    std::error_code ec;
    fs::directory_iterator it(directory, ec);
    if (ec)
        return false;
    const fs::directory_iterator end;
    while (it != end)
    {
        if (mviewer::core::pathToUtf8(it->path().filename()) == name)
            foundExact = true;
        it.increment(ec);
        if (ec)
            return false;
    }
    return true;
}

// The destination exists only because lookup is case-insensitive, and the
// directory entry is still stored under the source spelling.
bool isCaseOnlySameFile(const fs::path &source, const fs::path &destination,
                        const mviewer::core::FileSystemAdapter &fileSystem)
{
    const std::string sourceName = mviewer::core::pathToUtf8(source.filename());
    const std::string destinationName = mviewer::core::pathToUtf8(destination.filename());
    if (sourceName.empty() || sourceName == destinationName)
        return false;

    std::error_code ec;
    if (!fileSystem.exists(source, ec) || ec)
        return false;
    if (!fileSystem.exists(destination, ec) || ec)
        return false;

    bool foundExact = false;
    const bool listed = directoryHasExactName(source.parent_path(), destinationName, foundExact);
    // A real directory entry with the requested spelling is a collision, even
    // when it is a hard link to the source.
    if (listed && foundExact)
        return false;

    const bool same = fs::equivalent(source, destination, ec);
    if (!ec)
        return same;
    // exists() saw the destination, but no directory entry uses that spelling
    // (a case-insensitive volume, or a test adapter that case-folds exists()).
    // When equivalent() cannot confirm identity, require an ASCII case-only
    // name change so a failed stat does not hide a different file.
    return listed && asciiEqualFold(sourceName, destinationName);
}

fs::path caseRenameTemporary(const fs::path &source,
                             const std::shared_ptr<mviewer::core::FileSystemAdapter> &fileSystem)
{
    const std::string base = mviewer::core::pathToUtf8(source.filename());
    const fs::path parent = source.parent_path();
    for (int n = 0; n < 1000; ++n)
    {
        const fs::path candidate =
            parent / mviewer::core::pathFromUtf8(base + ".__mvcase__" + std::to_string(n));
        std::error_code ec;
        if (!fileSystem->exists(candidate, ec) && !ec)
            return candidate;
    }
    return {};
}

} // namespace

FileRenameCommand::FileRenameCommand(std::string oldPath, std::string newPath,
                                     std::shared_ptr<mviewer::core::FileSystemAdapter> fileSystem)
    : m_oldPath(std::move(oldPath)), m_newPath(std::move(newPath)),
      m_fileSystem(fileSystem ? std::move(fileSystem) : mviewer::core::defaultFileSystemAdapter())
{
}

std::string FileRenameCommand::description() const
{
    return "Rename " +
           mviewer::core::pathToUtf8(mviewer::core::pathFromUtf8(m_oldPath).filename()) + " → " +
           mviewer::core::pathToUtf8(mviewer::core::pathFromUtf8(m_newPath).filename());
}

bool FileRenameCommand::commitCaseOnlyRename(const fs::path &from, const fs::path &to,
                                             const char *failurePrefix)
{
    const fs::path temporary = caseRenameTemporary(from, m_fileSystem);
    if (temporary.empty())
    {
        m_lastError = std::string(failurePrefix) +
                      "Cannot inspect destination: " + mviewer::core::pathToUtf8(from);
        return false;
    }

    const auto first = mviewer::core::moveFileSafely(from, temporary, m_fileSystem);
    if (first.state != mviewer::core::FileTransferState::Succeeded)
    {
        m_lastError = std::string(failurePrefix) + first.error;
        if (first.state == mviewer::core::FileTransferState::Partial)
        {
            m_executed = true;
            m_unresolved = true;
            m_caseRenameTemp = mviewer::core::pathToUtf8(temporary);
            m_lastError += " The source/destination state is retained for recovery.";
        }
        return false;
    }

    const auto second = mviewer::core::moveFileSafely(temporary, to, m_fileSystem);
    if (second.state == mviewer::core::FileTransferState::Succeeded)
    {
        m_caseRenameTemp.clear();
        return true;
    }

    const auto back = mviewer::core::moveFileSafely(temporary, from, m_fileSystem);
    if (back.state == mviewer::core::FileTransferState::Succeeded)
    {
        m_lastError = std::string(failurePrefix) + second.error;
        return false;
    }

    m_executed = true;
    m_unresolved = true;
    m_caseRenameTemp = mviewer::core::pathToUtf8(temporary);
    m_lastError = std::string(failurePrefix) + second.error +
                  " The source/destination state is retained for recovery.";
    return false;
}

void FileRenameCommand::execute()
{
    if (!canExecute())
        return;

    m_lastError.clear();
    m_executed = false;
    m_unresolved = false;
    m_caseRenameTemp.clear();
    const fs::path oldPath = mviewer::core::pathFromUtf8(m_oldPath);
    const fs::path newPath = mviewer::core::pathFromUtf8(m_newPath);
    const fs::path fileName = newPath.filename();
    if (fileName.empty() || fileName == "." || fileName == ".." ||
        oldPath.parent_path().lexically_normal() != newPath.parent_path().lexically_normal())
    {
        m_lastError = "Rename destination must stay in the same directory: " + m_newPath;
        return;
    }
    std::error_code ec;
    if (!m_fileSystem->exists(oldPath, ec))
    {
        m_lastError = "Source file does not exist: " + m_oldPath;
        return;
    }
    if (!m_fileSystem->isRegularFile(oldPath, ec))
    {
        m_lastError = "Source is not a regular file: " + m_oldPath;
        return;
    }
    if (m_fileSystem->exists(newPath, ec))
    {
        if (isCaseOnlySameFile(oldPath, newPath, *m_fileSystem))
        {
            if (!commitCaseOnlyRename(oldPath, newPath, "Rename failed: "))
                return;
            m_executed = true;
            mviewer::core::relocateFileMetadata(m_oldPath, m_newPath);
            return;
        }
        m_lastError = "Destination already exists: " + m_newPath;
        return;
    }
    if (ec)
    {
        m_lastError = "Cannot inspect destination: " + m_newPath + ": " + ec.message();
        return;
    }

    const auto result = mviewer::core::moveFileSafely(oldPath, newPath, m_fileSystem);
    if (result.state == mviewer::core::FileTransferState::Succeeded)
    {
        m_executed = true;
        mviewer::core::relocateFileMetadata(m_oldPath, m_newPath);
        return;
    }

    m_lastError = "Rename failed: " + result.error;
    if (result.state == mviewer::core::FileTransferState::Partial)
    {
        m_executed = true;
        m_unresolved = true;
        m_lastError += " The source/destination state is retained for recovery.";
    }
}

void FileRenameCommand::undo()
{
    if (!canUndo())
        return;

    m_lastError.clear();
    const fs::path oldPath = mviewer::core::pathFromUtf8(m_oldPath);
    const fs::path newPath = mviewer::core::pathFromUtf8(m_newPath);
    std::error_code ec;
    if (!m_caseRenameTemp.empty())
    {
        const fs::path temporary = mviewer::core::pathFromUtf8(m_caseRenameTemp);
        if (!m_fileSystem->exists(temporary, ec))
        {
            m_lastError = "Renamed file no longer exists: " + m_caseRenameTemp;
            m_unresolved = true;
            return;
        }
        if (m_fileSystem->exists(oldPath, ec))
        {
            m_lastError = "Original name is now occupied: " + m_oldPath;
            m_unresolved = true;
            return;
        }
        const auto result = mviewer::core::moveFileSafely(temporary, oldPath, m_fileSystem);
        if (result.state != mviewer::core::FileTransferState::Succeeded)
        {
            m_lastError = "Undo rename failed: " + result.error;
            m_unresolved = result.state == mviewer::core::FileTransferState::Partial;
            return;
        }
        m_caseRenameTemp.clear();
        m_executed = false;
        m_unresolved = false;
        return;
    }

    const bool caseOnly = isCaseOnlySameFile(newPath, oldPath, *m_fileSystem);
    if (!caseOnly && m_fileSystem->exists(oldPath, ec))
    {
        m_lastError = "Original name is now occupied: " + m_oldPath;
        m_unresolved = true;
        return;
    }
    if (!m_fileSystem->exists(newPath, ec))
    {
        m_lastError = "Renamed file no longer exists: " + m_newPath;
        m_unresolved = true;
        return;
    }

    if (caseOnly)
    {
        if (!commitCaseOnlyRename(newPath, oldPath, "Undo rename failed: "))
            return;
        mviewer::core::relocateFileMetadata(m_newPath, m_oldPath);
        m_executed = false;
        m_unresolved = false;
        return;
    }

    const auto result = mviewer::core::moveFileSafely(newPath, oldPath, m_fileSystem);
    if (result.state != mviewer::core::FileTransferState::Succeeded)
    {
        m_lastError = "Undo rename failed: " + result.error;
        m_unresolved = result.state == mviewer::core::FileTransferState::Partial;
        return;
    }
    mviewer::core::relocateFileMetadata(m_newPath, m_oldPath);
    m_executed = false;
    m_unresolved = false;
}
