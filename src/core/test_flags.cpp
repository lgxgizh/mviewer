// P3 tail — RatingStore flag persistence tests (color label / reject / pick / recents).
#include "core/RatingStore.h"

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

using mviewer::core::RatingStore;

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    printf("\n[P3 flags]\n");

    // Isolate ratings/flags under a unique temp dir. Relative paths share
    // ./flags.txt with parallel ratingstore_tests under ctest -j and race.
    QTemporaryDir tmp;
    CHECK(tmp.isValid(), "temp dir for flags persistence");
    const std::string ratingsPath =
        QDir(tmp.path()).filePath(QStringLiteral("ratings.txt")).toUtf8().toStdString();

    RatingStore &s = RatingStore::instance();
    s.setFilePath(ratingsPath);

    s.setColorLabel("a.png", 2);
    s.setRejected("a.png", true);
    s.setPicked("b.png", true);
    s.addRecent("c.png");
    s.addRecent("d.png");

    // Explicit flush, then reload from the same isolated path.
    CHECK(s.save(), "explicit flush before reload");
    s.setFilePath(ratingsPath);

    CHECK(s.colorLabel("a.png") == 2, "colorLabel persisted (2)");
    CHECK(s.rejected("a.png") == true, "rejected persisted");
    CHECK(s.picked("b.png") == true, "picked persisted");
    CHECK(s.colorLabel("b.png") == 0, "untouched image has no label");

    auto recents = s.recents();
    bool hasC = false, hasD = false;
    for (const auto &r : recents)
    {
        if (r == "c.png")
            hasC = true;
        if (r == "d.png")
            hasD = true;
    }
    CHECK(hasC && hasD, "recents persisted (c, d)");

    // Clamp behaviour.
    s.setColorLabel("e.png", 99);
    CHECK(s.colorLabel("e.png") == 6, "colorLabel clamps to 6");
    s.setColorLabel("e.png", -5);
    CHECK(s.colorLabel("e.png") == 0, "negative label clears (0)");

    s.setPicked("b.png", false);
    CHECK(s.picked("b.png") == false, "pick toggled off");

    s.setRejected("a.png", false);
    CHECK(s.rejected("a.png") == false, "reject toggled off");

    // addRecent de-duplicates and moves to front.
    s.addRecent("c.png");
    auto recents2 = s.recents();
    CHECK(!recents2.empty() && recents2.front() == "c.png", "addRecent moves to front / dedupes");

    // CRLF flags file: the trailing CR must not stick to the path, and the
    // key is the same one lookup uses (cleanPath + slash fold). Load it via a
    // different store path — setFilePath flushes the current file first, so
    // rewriting the live flags.txt would be overwritten by that flush.
    {
        QTemporaryDir crlfDir;
        CHECK(crlfDir.isValid(), "CRLF flags temp dir");
        const QString flagsPath = QDir(crlfDir.path()).filePath(QStringLiteral("flags.txt"));
        QFile flags(flagsPath);
        CHECK(flags.open(QIODevice::WriteOnly | QIODevice::Truncate), "rewrite flags as CRLF");
        const QByteArray crlf("L|C:\\Photos\\..\\Photos\\shot.jpg|4\r\nX|C:/Photos/shot.jpg\r\n");
        CHECK(flags.write(crlf) == static_cast<qint64>(crlf.size()), "CRLF flags bytes written");
        flags.close();
        const QString crlfRatings = QDir(crlfDir.path()).filePath(QStringLiteral("ratings.txt"));
        s.setFilePath(crlfRatings.toStdString());
        CHECK(s.colorLabel("C:/Photos/shot.jpg") == 4, "CRLF flags label uses normalizeKey");
        CHECK(s.rejected("C:/Photos/shot.jpg"), "CRLF flags reject uses normalizeKey");
    }

    printf("\n=== P3 flags: %d passed, %d failed ===\n", g_pass, g_fail);
    fflush(stdout);
    return g_fail == 0 ? 0 : 1;
}
