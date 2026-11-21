// FileSystemAdapter transient-error retry: rename/remove must succeed once a
// reader that holds the file (on Windows without FILE_SHARE_DELETE, as QFile
// opens it) lets go within the bounded backoff.
#include "core/command/FileSystemAdapter.h"

#include <QCoreApplication>
#include <QFile>
#include <QString>
#include <QTemporaryDir>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <string>
#include <system_error>
#include <thread>

namespace fs = std::filesystem;

static int g_pass = 0;
static int g_fail = 0;

#define CHECK(cond, msg)                                                                           \
    do                                                                                             \
    {                                                                                              \
        if (cond)                                                                                  \
        {                                                                                          \
            ++g_pass;                                                                              \
        }                                                                                          \
        else                                                                                       \
        {                                                                                          \
            ++g_fail;                                                                              \
            printf("  FAIL: %s\n", msg);                                                           \
        }                                                                                          \
    } while (0)

static bool writeFile(const QString &path)
{
    QFile f(path);
    return f.open(QIODevice::WriteOnly) && f.write("data", 4) == 4;
}

// Holds `path` open for `holdMs` on a worker thread; returns once it is open.
class FileHolder
{
  public:
    FileHolder(const QString &path, int holdMs)
        : m_thread(
              [this, path, holdMs]()
              {
                  QFile file(path);
                  const bool ok = file.open(QIODevice::ReadOnly);
                  m_opened.store(ok);
                  m_ready.store(true);
                  if (ok)
                      std::this_thread::sleep_for(std::chrono::milliseconds(holdMs));
              })
    {
        const auto start = std::chrono::steady_clock::now();
        while (!m_ready.load() &&
               std::chrono::steady_clock::now() - start < std::chrono::seconds(2))
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    ~FileHolder()
    {
        if (m_thread.joinable())
            m_thread.join();
    }
    FileHolder(const FileHolder &) = delete;
    FileHolder &operator=(const FileHolder &) = delete;
    bool opened() const
    {
        return m_opened.load();
    }

  private:
    std::atomic<bool> m_ready{false};
    std::atomic<bool> m_opened{false};
    std::thread m_thread;
};

static fs::path toPath(const QString &path)
{
    return fs::path(path.toStdWString());
}

static void testRemoveWhileHeld(const QTemporaryDir &dir)
{
    printf("\n[remove while another handle holds the file]\n");
    const QString path = dir.filePath(QStringLiteral("held_remove.png"));
    CHECK(writeFile(path), "write held_remove.png");
    auto adapter = mviewer::core::defaultFileSystemAdapter();
    std::error_code ec;
    {
        FileHolder holder(path, 300);
        CHECK(holder.opened(), "holder opened the file");
        CHECK(adapter->remove(toPath(path), ec), "remove succeeds after the holder closes");
    }
    CHECK(!ec, "remove reports no error");
    CHECK(!QFile::exists(path), "file is gone");

    std::error_code missing;
    CHECK(!adapter->remove(toPath(path), missing), "removing a missing file returns false");
    CHECK(!missing, "removing a missing file is not an error");
}

static void testRenameWhileHeld(const QTemporaryDir &dir)
{
    printf("\n[rename while another handle holds the file]\n");
    const QString path = dir.filePath(QStringLiteral("held_rename.png"));
    const QString target = dir.filePath(QStringLiteral("held_renamed.png"));
    CHECK(writeFile(path), "write held_rename.png");
    auto adapter = mviewer::core::defaultFileSystemAdapter();
    std::error_code ec;
    {
        FileHolder holder(path, 300);
        CHECK(holder.opened(), "holder opened the file");
        CHECK(adapter->rename(toPath(path), toPath(target), ec),
              "rename succeeds after the holder closes");
    }
    CHECK(!ec, "rename reports no error");
    CHECK(!QFile::exists(path) && QFile::exists(target), "file moved to the new name");
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    printf("=== FileSystemAdapter retry tests ===\n");
    QTemporaryDir dir;
    CHECK(dir.isValid(), "temp dir created");
    testRemoveWhileHeld(dir);
    testRenameWhileHeld(dir);
    printf("\n=== Results: %d passed, %d failed ===\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
