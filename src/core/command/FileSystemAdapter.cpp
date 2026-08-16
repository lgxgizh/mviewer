#include "FileSystemAdapter.h"

#include <atomic>
#include <fstream>
#include <system_error>
#include <vector>

namespace fs = std::filesystem;

namespace mviewer::core
{
namespace
{

class StdFileSystemAdapter final : public FileSystemAdapter
{
  public:
    bool exists(const fs::path &path, std::error_code &ec) const override
    {
        return fs::exists(path, ec);
    }

    bool isDirectory(const fs::path &path, std::error_code &ec) const override
    {
        return fs::is_directory(path, ec);
    }

    bool isRegularFile(const fs::path &path, std::error_code &ec) const override
    {
        return fs::is_regular_file(path, ec);
    }

    bool createDirectories(const fs::path &path, std::error_code &ec) override
    {
        return fs::create_directories(path, ec);
    }

    bool rename(const fs::path &from, const fs::path &to, std::error_code &ec) override
    {
        fs::rename(from, to, ec);
        return !ec;
    }

    bool copyFile(const fs::path &from, const fs::path &to, std::error_code &ec) override
    {
        return fs::copy_file(from, to, fs::copy_options::none, ec);
    }

    bool copyFileWithProgress(const fs::path &from, const fs::path &to,
                              const TransferObserver &observer, std::error_code &ec) override
    {
        std::error_code sizeError;
        const uintmax_t total = fs::file_size(from, sizeError);
        if (sizeError)
        {
            ec = sizeError;
            return false;
        }
        if (observer && !observer(0, total))
        {
            ec = std::make_error_code(std::errc::operation_canceled);
            return false;
        }

        std::ifstream input(from, std::ios::binary);
        std::ofstream output(to, std::ios::binary | std::ios::trunc);
        if (!input || !output)
        {
            ec = std::make_error_code(std::errc::io_error);
            return false;
        }

        constexpr std::size_t kChunkSize = 1024 * 1024;
        std::vector<char> buffer(kChunkSize);
        uintmax_t copied = 0;
        while (input)
        {
            input.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
            const std::streamsize count = input.gcount();
            if (count > 0)
            {
                output.write(buffer.data(), count);
                if (!output)
                {
                    ec = std::make_error_code(std::errc::io_error);
                    return false;
                }
                copied += static_cast<uintmax_t>(count);
                if (observer && !observer(copied, total))
                {
                    ec = std::make_error_code(std::errc::operation_canceled);
                    return false;
                }
            }
        }
        if (input.bad())
        {
            ec = std::make_error_code(std::errc::io_error);
            return false;
        }
        output.close();
        if (!output)
        {
            ec = std::make_error_code(std::errc::io_error);
            return false;
        }
        return true;
    }

    bool remove(const fs::path &path, std::error_code &ec) override
    {
        return fs::remove(path, ec);
    }

    uintmax_t fileSize(const fs::path &path, std::error_code &ec) const override
    {
        return fs::file_size(path, ec);
    }
};

std::atomic_uint64_t g_tempSequence = 0;

fs::path temporaryPathFor(const fs::path &destination)
{
    const auto sequence = ++g_tempSequence;
    fs::path temporary = destination;
    temporary += ".mviewer-part-" + std::to_string(sequence);
    return temporary;
}

bool isCrossVolume(const std::error_code &ec)
{
    return ec == std::make_error_code(std::errc::cross_device_link);
}

std::string describeError(const std::string &operation, const fs::path &path,
                          const std::error_code &ec)
{
    return operation + " '" + pathToUtf8(path) + "': " +
           (ec ? ec.message() : std::string("unknown error"));
}

} // namespace

bool FileSystemAdapter::copyFileWithProgress(const fs::path &from, const fs::path &to,
                                              const TransferObserver &observer,
                                              std::error_code &ec)
{
    std::error_code sizeError;
    const uintmax_t total = fileSize(from, sizeError);
    if (sizeError)
    {
        ec = sizeError;
        return false;
    }
    if (observer && !observer(0, total))
    {
        ec = std::make_error_code(std::errc::operation_canceled);
        return false;
    }
    if (!copyFile(from, to, ec))
        return false;
    if (observer && !observer(total, total))
    {
        ec = std::make_error_code(std::errc::operation_canceled);
        return false;
    }
    return true;
}

fs::path pathFromUtf8(const std::string &path)
{
    return fs::u8path(path);
}

std::string pathToUtf8(const fs::path &path)
{
    const auto value = path.u8string();
    return {reinterpret_cast<const char *>(value.data()), value.size()};
}

std::shared_ptr<FileSystemAdapter> defaultFileSystemAdapter()
{
    static const auto adapter = std::make_shared<StdFileSystemAdapter>();
    return adapter;
}

bool samePath(const fs::path &left, const fs::path &right,
              const std::shared_ptr<FileSystemAdapter> &adapter)
{
    std::error_code ec;
    if (adapter->exists(left, ec) && adapter->exists(right, ec))
    {
        const fs::path canonicalLeft = std::filesystem::weakly_canonical(left, ec);
        if (!ec)
        {
            const fs::path canonicalRight = std::filesystem::weakly_canonical(right, ec);
            if (!ec)
                return canonicalLeft == canonicalRight;
        }
    }
    return left.lexically_normal() == right.lexically_normal();
}

fs::path collisionFreeDestination(const fs::path &source, const fs::path &directory,
                                   const std::shared_ptr<FileSystemAdapter> &adapter,
                                   std::string &error)
{
    error.clear();
    std::error_code ec;
    if (directory.empty())
    {
        error = "Destination directory is empty.";
        return {};
    }
    if (adapter->exists(directory, ec))
    {
        if (!adapter->isDirectory(directory, ec))
        {
            error = "Destination is not a directory: " + pathToUtf8(directory);
            return {};
        }
    }
    else if (ec)
    {
        error = describeError("Cannot inspect destination directory", directory, ec);
        return {};
    }

    const fs::path requested = directory / source.filename();
    if (samePath(source, requested, adapter))
    {
        error = "Source and destination are the same path: " + pathToUtf8(source);
        return {};
    }

    fs::path candidate = requested;
    int suffix = 0;
    while (adapter->exists(candidate, ec))
    {
        if (ec)
        {
            error = describeError("Cannot inspect destination", candidate, ec);
            return {};
        }
        ++suffix;
        const std::string stem = pathToUtf8(source.stem());
        const std::string extension = pathToUtf8(source.extension());
        candidate = directory / pathFromUtf8(stem + "_" + std::to_string(suffix) + extension);
    }
    if (ec)
    {
        error = describeError("Cannot inspect destination", candidate, ec);
        return {};
    }
    return candidate;
}

FileTransferResult copyFileAtomically(const fs::path &source, const fs::path &destination,
                                      const std::shared_ptr<FileSystemAdapter> &adapter)
{
    return copyFileAtomically(source, destination, adapter, {});
}

FileTransferResult copyFileAtomically(const fs::path &source, const fs::path &destination,
                                      const std::shared_ptr<FileSystemAdapter> &adapter,
                                      const FileSystemAdapter::TransferObserver &observer)
{
    std::error_code ec;
    if (adapter->exists(destination, ec))
        return {FileTransferState::Failed,
                "Destination already exists: " + pathToUtf8(destination)};
    if (ec)
        return {FileTransferState::Failed,
                describeError("Cannot inspect destination", destination, ec)};

    const fs::path temporary = temporaryPathFor(destination);
    if (!adapter->copyFileWithProgress(source, temporary, observer, ec))
    {
        std::error_code cleanup;
        adapter->remove(temporary, cleanup);
        if (cleanup)
            return {FileTransferState::Partial,
                    describeError("Copy failed and temporary cleanup failed", temporary, cleanup)};
        if (ec == std::make_error_code(std::errc::operation_canceled))
            return {FileTransferState::Failed, "Copy cancelled."};
        return {FileTransferState::Failed, describeError("Copy failed", source, ec)};
    }

    std::error_code sourceSizeError;
    std::error_code destinationSizeError;
    const uintmax_t sourceSize = adapter->fileSize(source, sourceSizeError);
    const uintmax_t destinationSize = adapter->fileSize(temporary, destinationSizeError);
    if (sourceSizeError || destinationSizeError || sourceSize != destinationSize)
    {
        std::error_code cleanup;
        adapter->remove(temporary, cleanup);
        const std::string reason = sourceSizeError
                                       ? sourceSizeError.message()
                                       : destinationSizeError
                                             ? destinationSizeError.message()
                                             : "byte count differs from source";
        if (cleanup)
            return {FileTransferState::Partial,
                    "Copy verification failed (" + reason + ") and temporary cleanup failed."};
        return {FileTransferState::Failed, "Copy verification failed: " + reason};
    }

    if (!adapter->rename(temporary, destination, ec))
    {
        std::error_code cleanup;
        adapter->remove(temporary, cleanup);
        if (cleanup)
            return {FileTransferState::Partial,
                    describeError("Atomic copy commit failed and cleanup failed", destination, ec)};
        return {FileTransferState::Failed, describeError("Atomic copy commit failed", destination, ec)};
    }
    return {FileTransferState::Succeeded, {}};
}

FileTransferResult moveFileSafely(const fs::path &source, const fs::path &destination,
                                  const std::shared_ptr<FileSystemAdapter> &adapter)
{
    return moveFileSafely(source, destination, adapter, {});
}

FileTransferResult moveFileSafely(const fs::path &source, const fs::path &destination,
                                  const std::shared_ptr<FileSystemAdapter> &adapter,
                                  const FileSystemAdapter::TransferObserver &observer)
{
    std::error_code ec;
    if (observer && !observer(0, 0))
        return {FileTransferState::Failed, "Move cancelled."};
    if (samePath(source, destination, adapter))
        return {FileTransferState::Failed,
                "Source and destination are the same path: " + pathToUtf8(source)};
    if (!adapter->exists(source, ec))
        return {FileTransferState::Failed, describeError("Source does not exist", source, ec)};
    if (!adapter->isRegularFile(source, ec))
        return {FileTransferState::Failed, "Source is not a regular file: " + pathToUtf8(source)};
    if (adapter->exists(destination, ec))
        return {FileTransferState::Failed,
                "Destination already exists: " + pathToUtf8(destination)};
    if (ec)
        return {FileTransferState::Failed,
                describeError("Cannot inspect destination", destination, ec)};

    if (adapter->rename(source, destination, ec))
    {
        if (observer && !observer(1, 1))
            return {FileTransferState::Succeeded, {}};
        return {FileTransferState::Succeeded, {}};
    }
    if (!isCrossVolume(ec))
        return {FileTransferState::Failed, describeError("Move failed", source, ec)};

    const FileTransferResult copied = copyFileAtomically(source, destination, adapter, observer);
    if (copied.state != FileTransferState::Succeeded)
        return copied;

    if (observer && !observer(1, 1))
    {
        std::error_code cleanup;
        adapter->remove(destination, cleanup);
        if (!cleanup)
            return {FileTransferState::Failed, "Move cancelled."};
        return {FileTransferState::Partial,
                "Move cancelled and destination cleanup failed: " + cleanup.message()};
    }

    if (adapter->remove(source, ec))
        return {FileTransferState::Succeeded, {}};

    std::error_code cleanup;
    adapter->remove(destination, cleanup);
    if (!cleanup)
        return {FileTransferState::Failed, describeError("Source cleanup failed", source, ec)};
    return {FileTransferState::Partial,
            describeError("Source cleanup failed and destination rollback failed", source, ec)};
}

} // namespace mviewer::core
