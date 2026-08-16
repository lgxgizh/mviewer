#pragma once

#include <filesystem>
#include <functional>
#include <memory>
#include <string>

namespace mviewer::core
{

// Narrow command paths are UTF-8 at the Qt/core boundary. Keeping conversion
// here avoids relying on the process locale when std::filesystem targets
// Windows native paths.
std::filesystem::path pathFromUtf8(const std::string &path);
std::string pathToUtf8(const std::filesystem::path &path);

class FileSystemAdapter
{
  public:
    // Return false to cancel the current transfer. The callback is invoked
    // from the worker performing the copy and must not touch Qt widgets.
    using TransferObserver = std::function<bool(uintmax_t copied, uintmax_t total)>;

    virtual ~FileSystemAdapter() = default;

    virtual bool exists(const std::filesystem::path &path, std::error_code &ec) const = 0;
    virtual bool isDirectory(const std::filesystem::path &path, std::error_code &ec) const = 0;
    virtual bool isRegularFile(const std::filesystem::path &path, std::error_code &ec) const = 0;
    virtual bool createDirectories(const std::filesystem::path &path, std::error_code &ec) = 0;
    virtual bool rename(const std::filesystem::path &from, const std::filesystem::path &to,
                        std::error_code &ec) = 0;
    virtual bool copyFile(const std::filesystem::path &from, const std::filesystem::path &to,
                          std::error_code &ec) = 0;
    // Optional cancellable/progressive copy seam. Existing fault-injected
    // adapters only need to implement copyFile(); the default preserves their
    // behavior and reports a coarse 0/100 progress pair.
    virtual bool copyFileWithProgress(const std::filesystem::path &from,
                                      const std::filesystem::path &to,
                                      const TransferObserver &observer,
                                      std::error_code &ec);
    virtual bool remove(const std::filesystem::path &path, std::error_code &ec) = 0;
    virtual uintmax_t fileSize(const std::filesystem::path &path, std::error_code &ec) const = 0;
};

std::shared_ptr<FileSystemAdapter> defaultFileSystemAdapter();

enum class FileTransferState
{
    Succeeded,
    Failed,
    Partial
};

struct FileTransferResult
{
    FileTransferState state = FileTransferState::Failed;
    std::string error;
};

// Copies to a temporary file in the destination directory, verifies the byte
// count, and commits with a same-volume rename. Existing destinations are
// never overwritten.
FileTransferResult copyFileAtomically(const std::filesystem::path &source,
                                      const std::filesystem::path &destination,
                                      const std::shared_ptr<FileSystemAdapter> &fs);
FileTransferResult copyFileAtomically(const std::filesystem::path &source,
                                      const std::filesystem::path &destination,
                                      const std::shared_ptr<FileSystemAdapter> &fs,
                                      const FileSystemAdapter::TransferObserver &observer);

// Uses rename when possible. On a cross-volume error it falls back to the
// verified copy/commit path and removes the source only after the destination
// is committed. Partial means the caller must retain bookkeeping for recovery.
FileTransferResult moveFileSafely(const std::filesystem::path &source,
                                  const std::filesystem::path &destination,
                                  const std::shared_ptr<FileSystemAdapter> &fs);
FileTransferResult moveFileSafely(const std::filesystem::path &source,
                                  const std::filesystem::path &destination,
                                  const std::shared_ptr<FileSystemAdapter> &fs,
                                  const FileSystemAdapter::TransferObserver &observer);

bool samePath(const std::filesystem::path &left, const std::filesystem::path &right,
              const std::shared_ptr<FileSystemAdapter> &fs);

std::filesystem::path collisionFreeDestination(const std::filesystem::path &source,
                                               const std::filesystem::path &directory,
                                               const std::shared_ptr<FileSystemAdapter> &fs,
                                               std::string &error);

} // namespace mviewer::core
