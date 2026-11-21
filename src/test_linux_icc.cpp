// Item 4: non-Windows ICC. On Linux the monitor profile lives with colord or in
// the usual color directories, so the platform loader looks there (and honours
// $MVIEWER_ICC as an explicit override). On any other platform this is a skip.
#include "display/DisplayColorContextProvider.h"

#include <QApplication>
#include <QDir>
#include <QFile>
#include <QTemporaryDir>

#include <cstdio>

#if defined(Q_OS_LINUX)

namespace
{
int g_pass = 0;
int g_fail = 0;

#define CHECK(cond, msg)                                                                           \
    do                                                                                             \
    {                                                                                              \
        if (cond)                                                                                  \
        {                                                                                          \
            printf("  PASS: %s\n", msg);                                                           \
            ++g_pass;                                                                              \
        }                                                                                          \
        else                                                                                       \
        {                                                                                          \
            printf("  FAIL: %s\n", msg);                                                           \
            ++g_fail;                                                                              \
        }                                                                                          \
    } while (false)
} // namespace

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    setbuf(stdout, nullptr);
    QTemporaryDir tmp;
    CHECK(tmp.isValid(), "temp dir created");
    if (!tmp.isValid())
        return 1;

    const QByteArray fixture(4096, '\x02');
    const QString profilePath = tmp.filePath(QStringLiteral("display.icc"));
    {
        QFile file(profilePath);
        CHECK(file.open(QIODevice::WriteOnly | QIODevice::Truncate), "profile fixture written");
        file.write(fixture);
        file.close();
    }

    qputenv("MVIEWER_ICC", profilePath.toUtf8());
    DisplayColorContextProvider::resetForTest();
    const auto context = DisplayColorContextProvider::forWindow(nullptr);
    CHECK(context.hasProfile(), "the override profile reaches the display context");
    const QByteArray loaded(
        context.iccProfile.empty()
            ? QByteArray()
            : QByteArray::fromRawData(reinterpret_cast<const char *>(context.iccProfile.data()),
                                      static_cast<int>(context.iccProfile.size())));
    CHECK(loaded == fixture, "the profile bytes round-trip untouched");

    // With no override and no system color directory, the loader degrades to the
    // sRGB fallback rather than inventing a profile.
    qunsetenv("MVIEWER_ICC");
    DisplayColorContextProvider::resetForTest();
    const auto fallback = DisplayColorContextProvider::forWindow(nullptr);
    CHECK(!fallback.hasProfile(), "no profile available means the sRGB fallback");

    printf("\n==== linux icc test: %d passed, %d failed ====\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
#else
int main()
{
    printf("linux_icc_tests: SKIP (platform has no colord loader)\n");
    return 0;
}
#endif
