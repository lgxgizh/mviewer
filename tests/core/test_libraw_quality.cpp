// NOLINTBEGIN
// P2-10: LibRaw process params for the default path and QSettings raw/highQuality.
// No camera RAW file. The store is a temp Ini file under a unique org/app.

#include "core/image/decoder/RawDecoder.h"

#include <QCoreApplication>
#include <QSettings>
#include <QString>
#include <QTemporaryDir>

#include <cstdio>
#include <cstring>

static int g_failures = 0;

#define CHECK(cond)                                                                                \
    do                                                                                             \
    {                                                                                              \
        if (!(cond))                                                                               \
        {                                                                                          \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);                            \
            ++g_failures;                                                                          \
        }                                                                                          \
    } while (0)

namespace
{

QString highQualityKey()
{
    return QString::fromLatin1(RawDecoder::kRawHighQualitySettingsKey);
}

void expectParams(bool halfSize, int userQual, int outputBps, int highlight, const char *label)
{
    const RawDecoder::LibrawProcessParams params = RawDecoder::librawProcessParamsForTest(halfSize);
    const int wantHalf = halfSize ? 1 : 0;
    if (params.halfSize == wantHalf && params.userQual == userQual &&
        params.outputBps == outputBps && params.highlight == highlight &&
        params.noAutoBright == 1 && params.useCameraWb == 1)
        return;
    std::printf("FAIL %s: half=%d qual=%d bps=%d highlight=%d no_auto_bright=%d "
                "use_camera_wb=%d (want half=%d qual=%d bps=%d highlight=%d)\n",
                label, params.halfSize, params.userQual, params.outputBps, params.highlight,
                params.noAutoBright, params.useCameraWb, wantHalf, userQual, outputBps, highlight);
    ++g_failures;
}

void expectDefault(const char *label)
{
    expectParams(false, 3, 8, 0, label);
    expectParams(true, 0, 8, 0, label);
}

void setHighQuality(bool enabled)
{
    QSettings settings;
    settings.setValue(highQualityKey(), enabled);
    settings.sync();
}

void removeHighQuality()
{
    QSettings settings;
    settings.remove(highQualityKey());
    settings.sync();
}

} // namespace

int main(int argc, char **argv)
{
    // Dir outlives QCoreApplication so a shutdown settings flush still has a path.
    QTemporaryDir dir;
    if (!dir.isValid())
    {
        std::printf("FAIL: temp settings dir\n");
        return 1;
    }
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, dir.path());

    QCoreApplication app(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("mviewer-libraw-quality-test"));
    QCoreApplication::setApplicationName(QStringLiteral("mviewer-libraw-quality-test"));

    CHECK(std::strcmp(RawDecoder::kRawHighQualitySettingsKey, "raw/highQuality") == 0);
    {
        QSettings settings;
        settings.clear();
        settings.sync();
        CHECK(settings.status() == QSettings::NoError);
        CHECK(!settings.contains(highQualityKey()));
    }

    expectDefault("default (key absent)");

    setHighQuality(true);
    CHECK(QSettings().value(highQualityKey(), false).toBool());
    expectParams(false, 12, 16, 2, "raw/highQuality=true full");
    // Thumbs stay linear / 8-bit / clip. Camera WB and no-auto-bright stay on.
    expectParams(true, 0, 8, 0, "raw/highQuality=true half-size");

    setHighQuality(false);
    CHECK(!QSettings().value(highQualityKey(), true).toBool());
    expectDefault("raw/highQuality=false");

    setHighQuality(true);
    removeHighQuality();
    CHECK(!QSettings().contains(highQualityKey()));
    expectDefault("key cleared");

    if (g_failures != 0)
    {
        std::printf("libraw_quality_tests: %d failed\n", g_failures);
        return 1;
    }
    std::printf("libraw_quality_tests: all passed\n");
    return 0;
}
// NOLINTEND
