// RecentStore — cap, dedupe, persistence, flags.txt migration, prune.
#include "core/RecentStore.h"
#include "core/filesystem/Utf8Path.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QTemporaryDir>

#include <cstdio>
#include <string>

static int g_pass = 0;
static int g_fail = 0;

#define CHECK(cond, msg)                                                                           \
    do                                                                                             \
    {                                                                                              \
        if (cond)                                                                                  \
        {                                                                                          \
            printf("  PASS: %s\n", msg);                                                           \
            g_pass++;                                                                              \
        }                                                                                          \
        else                                                                                       \
        {                                                                                          \
            printf("  FAIL: %s\n", msg);                                                           \
            g_fail++;                                                                              \
        }                                                                                          \
    } while (0)

using mviewer::core::RecentStore;

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    printf("\n[RecentStore]\n");

    QTemporaryDir tmp;
    CHECK(tmp.isValid(), "temp dir");
    const QString dir = tmp.path();
    const std::string recentsPath =
        QDir(dir).filePath(QStringLiteral("recents.txt")).toUtf8().toStdString();

    RecentStore &s = RecentStore::instance();
    s.setFilePath(recentsPath);

    // Cap at 200. The oldest entry falls off the back; the newest stays in front.
    for (int i = 0; i < RecentStore::kMaxRecents + 1; ++i)
        s.addRecent("img-" + std::to_string(i) + ".png");
    auto capped = s.recents();
    CHECK(static_cast<int>(capped.size()) == RecentStore::kMaxRecents, "cap is 200");
    CHECK(!capped.empty() && capped.front() == "img-200.png", "newest stays at front");
    bool droppedOldest = true;
    for (const auto &path : capped)
    {
        if (path == "img-0.png")
            droppedOldest = false;
    }
    CHECK(droppedOldest, "oldest entry past the cap is dropped");

    // Dedupe and move to front.
    s.addRecent("img-150.png");
    auto deduped = s.recents();
    CHECK(deduped.front() == "img-150.png", "addRecent moves to front");
    int count150 = 0;
    for (const auto &path : deduped)
    {
        if (path == "img-150.png")
            ++count150;
    }
    CHECK(count150 == 1, "addRecent dedupes");
    CHECK(static_cast<int>(deduped.size()) == RecentStore::kMaxRecents,
          "dedupe does not grow the cap");

    // Backslash and slash are one key. Start from a fresh file: setFilePath
    // flushes the capped list above to recentsPath first.
    const std::string foldPath =
        QDir(dir).filePath(QStringLiteral("recents-fold.txt")).toUtf8().toStdString();
    s.setFilePath(foldPath);
    const std::string folded = mviewer::core::normalizeKey("C:\\Photos\\a.png");
    s.addRecent("C:/Photos/a.png");
    s.addRecent("C:\\Photos\\a.png");
    auto foldedList = s.recents();
    CHECK(foldedList.size() == 1 && foldedList.front() == folded,
          "backslash and slash are one entry");
    CHECK(s.isRecent("C:/Photos/a.png"), "isRecent matches slash form");
    CHECK(s.isRecent("C:\\Photos\\a.png"), "isRecent matches backslash form");

    // Rename keeps position.
    s.addRecent("C:/Photos/c.png");
    s.addRecent("C:/Photos/b.png");
    s.addRecent("C:/Photos/a.png");
    // Order: a, b, c, then the earlier folded entry.
    const std::string bKey = mviewer::core::normalizeKey("C:/Photos/b.png");
    const std::string b2Key = mviewer::core::normalizeKey("C:/Photos/b2.png");
    auto beforeRename = s.recents();
    std::size_t bPos = beforeRename.size();
    for (std::size_t i = 0; i < beforeRename.size(); ++i)
    {
        if (beforeRename[i] == bKey)
            bPos = i;
    }
    s.renamePath("C:/Photos/b.png", "C:/Photos/b2.png");
    auto afterRename = s.recents();
    CHECK(bPos < afterRename.size() && afterRename[bPos] == b2Key, "rename keeps position");
    CHECK(!s.isRecent("C:/Photos/b.png"), "old key is gone after rename");

    // Persistence round trip: flush, then load again through setFilePath.
    CHECK(s.flush(), "flush writes recents.txt");
    s.setFilePath(foldPath);
    auto reloaded = s.recents();
    CHECK(reloaded == afterRename, "reload matches flushed list");

    // Migration from flags.txt. flags.txt must stay byte-identical.
    {
        QTemporaryDir mig;
        CHECK(mig.isValid(), "migration temp dir");
        const QString flagsPath = QDir(mig.path()).filePath(QStringLiteral("flags.txt"));
        const QByteArray original("N|C:/a.png\nN|C:/b.png\r\nL|C:/x.png|2\n");
        QFile flags(flagsPath);
        CHECK(flags.open(QIODevice::WriteOnly | QIODevice::Truncate), "write flags.txt");
        CHECK(flags.write(original) == static_cast<qint64>(original.size()), "flags.txt bytes");
        flags.close();

        const std::string migRecents =
            QDir(mig.path()).filePath(QStringLiteral("recents.txt")).toUtf8().toStdString();
        s.setFilePath(migRecents);
        auto migrated = s.recents();
        const std::string aKey = mviewer::core::normalizeKey("C:/a.png");
        const std::string bKeyMig = mviewer::core::normalizeKey("C:/b.png");
        CHECK(migrated.size() == 2 && migrated[0] == aKey && migrated[1] == bKeyMig,
              "migration yields exactly [a, b]");
        CHECK(!s.isRecent("C:/x.png"), "L| lines are not recents");

        QFile flagsAfter(flagsPath);
        CHECK(flagsAfter.open(QIODevice::ReadOnly), "reopen flags.txt");
        const QByteArray after = flagsAfter.readAll();
        flagsAfter.close();
        CHECK(after == original, "flags.txt is byte-identical");
        CHECK(QFile::exists(QString::fromStdString(migRecents)), "migration wrote recents.txt");
    }

    // pruneMissing drops paths that are not on disk and keeps a real file.
    {
        QTemporaryDir files;
        CHECK(files.isValid(), "prune temp dir");
        const QString real = QDir(files.path()).filePath(QStringLiteral("keep.png"));
        QFile created(real);
        CHECK(created.open(QIODevice::WriteOnly), "create keep.png");
        created.write("x");
        created.close();
        const std::string realKey = mviewer::core::normalizeKey(real.toUtf8().toStdString());
        const std::string missingKey = mviewer::core::normalizeKey("C:/no/such/recent.png");

        const std::string pruneRecents =
            QDir(files.path()).filePath(QStringLiteral("recents.txt")).toUtf8().toStdString();
        s.setFilePath(pruneRecents);
        s.addRecent(missingKey);
        s.addRecent(realKey);
        const std::size_t pruned = s.pruneMissing();
        CHECK(pruned == 1, "pruneMissing removed one missing file");
        auto left = s.recents();
        CHECK(left.size() == 1 && left.front() == realKey, "existing file stays");
    }

    printf("\n=== RecentStore: %d passed, %d failed ===\n", g_pass, g_fail);
    fflush(stdout);
    return g_fail == 0 ? 0 : 1;
}
