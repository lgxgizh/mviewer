// Audit regressions: preset path sanitizing (#41), tag-store path resolution
// (#45), and settings integer restore (#47).
#include "core/SettingsIO.h"
#include "core/TagStore.h"
#include "core/export/ExportManager.h"
#include "core/filesystem/Utf8Path.h"
#include "runtime_storage.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSettings>
#include <QStandardPaths>
#include <QTemporaryDir>

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <thread>

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

static void setEnvVar(const char *name, const QByteArray &value)
{
#ifdef _WIN32
    (void)_putenv_s(name, value.constData());
#else
    (void)::setenv(name, value.constData(), 1);
#endif
}

static bool pathInsidePresetDir(const std::string &path)
{
    const std::string dir = ExportManager::defaultPresetDir();
    if (dir.empty() || path.size() <= dir.size() || path.compare(0, dir.size(), dir) != 0)
        return false;
    const std::string rest = path.substr(dir.size());
    if (rest.size() < 2 || (rest[0] != '/' && rest[0] != '\\'))
        return false;
    const std::string leaf = rest.substr(1);
    return !leaf.empty() && leaf.find('/') == std::string::npos &&
           leaf.find('\\') == std::string::npos && leaf.find("..") == std::string::npos;
}

static void removePreset(const std::string &name)
{
    ExportManager::instance().deletePreset(name);
    const std::string path = ExportManager::presetPath(name);
    if (path.empty())
        return;
    std::error_code ec;
    std::filesystem::remove(mviewer::core::pathFromUtf8(path), ec);
}

static void testPresetPaths(const std::string &suffix)
{
    printf("\n[audit export presets]\n");
    ExportManager &manager = ExportManager::instance();

    ExportManager::Preset blank;
    CHECK(!manager.savePreset(blank), "empty preset name is rejected");

    const std::string evil = "../audit41_" + suffix;
    ExportManager::Preset preset;
    preset.name = evil;
    preset.format = "png";
    preset.quality = 81;
    CHECK(manager.savePreset(preset), "sanitized preset saves");
    const std::string stored = ExportManager::presetPath(evil);
    CHECK(pathInsidePresetDir(stored), "preset path stays inside the preset directory");
    std::error_code ec;
    CHECK(std::filesystem::is_regular_file(mviewer::core::pathFromUtf8(stored), ec),
          "preset file exists");
    const std::filesystem::path parent =
        mviewer::core::pathFromUtf8(ExportManager::defaultPresetDir()).parent_path();
    const std::filesystem::path escaped = parent / ("audit41_" + suffix + ".json");
    CHECK(!std::filesystem::exists(escaped, ec),
          "traversal name does not create a file outside the preset dir");
    const ExportManager::Preset loaded = manager.loadPreset(evil);
    CHECK(loaded.format == "png" && loaded.quality == 81, "sanitized preset round-trips");
    CHECK(loaded.name.find('/') == std::string::npos &&
              loaded.name.find('\\') == std::string::npos &&
              loaded.name.find("..") == std::string::npos,
          "loaded preset name has no path separators");

    ExportManager::Preset spaced;
    spaced.name = "My Preset-" + suffix;
    spaced.quality = 64;
    CHECK(manager.savePreset(spaced), "spaced preset name saves");
    CHECK(manager.loadPreset(spaced.name).quality == 64, "spaced preset round-trips");

    ExportManager::Preset cjk;
    // i18n-allow: non-ASCII name fixture (exercises Unicode handling)
    cjk.name = std::string("预设") + suffix;
    cjk.format = "jpeg";
    cjk.quality = 70;
    CHECK(manager.savePreset(cjk), "CJK preset name saves");
    CHECK(manager.loadPreset(cjk.name).name == cjk.name, "CJK preset name round-trips");

    ExportManager::Preset left;
    left.name = "AuditConcA_" + suffix;
    left.quality = 11;
    ExportManager::Preset right;
    right.name = "AuditConcB_" + suffix;
    right.quality = 22;
    std::atomic<int> saves{0};
    std::thread first(
        [&]
        {
            for (int i = 0; i < 20; ++i)
            {
                left.quality = 11 + (i % 3);
                if (manager.savePreset(left))
                    saves.fetch_add(1, std::memory_order_relaxed);
                (void)manager.loadPreset(left.name);
            }
        });
    std::thread second(
        [&]
        {
            for (int i = 0; i < 20; ++i)
            {
                if (manager.savePreset(right))
                    saves.fetch_add(1, std::memory_order_relaxed);
                (void)manager.loadPreset(right.name);
            }
        });
    first.join();
    second.join();
    CHECK(saves.load(std::memory_order_relaxed) == 40, "concurrent preset saves succeed");
    const ExportManager::Preset leftLoaded = manager.loadPreset(left.name);
    CHECK(leftLoaded.quality == 11 || leftLoaded.quality == 12 || leftLoaded.quality == 13,
          "concurrent preset A is intact");
    CHECK(manager.loadPreset(right.name).quality == 22, "concurrent preset B is intact");

    removePreset(evil);
    removePreset(spaced.name);
    removePreset(cjk.name);
    removePreset(left.name);
    removePreset(right.name);
}

static void testTagPathResolvedAfterApp(mviewer::core::TagStore &tags, bool cwdHadTags)
{
    printf("\n[audit tag store path]\n");
    const QString cwdTags = QDir::current().filePath(QStringLiteral("tags.txt"));
    const QString expected =
        mviewer::runtime::filePath(QStandardPaths::AppDataLocation, QStringLiteral("tags.txt"));
    CHECK(!expected.isEmpty(), "tags path resolves under the writable app data dir");
    const std::string image = "/tmp/audit45.png";
    tags.addTag(image, "keep");
    CHECK(tags.hasTag(image, "keep"), "tag is stored in memory");
    CHECK(tags.save(), "tag save writes the runtime path");
    CHECK(QFileInfo::exists(expected), "tags.txt was created in the app data dir");
    QFile file(expected);
    CHECK(file.open(QIODevice::ReadOnly), "open app data tags.txt");
    const QByteArray body = file.readAll();
    file.close();
    CHECK(QString::fromUtf8(body).contains(QStringLiteral("keep|")),
          "app data file contains the tag");
    CHECK(QFileInfo::exists(cwdTags) == cwdHadTags,
          "tags.txt was not created in the working directory");
}

static void testSettingsRejectBadIntegers()
{
    printf("\n[audit settings integer restore]\n");
    {
        QSettings seeded;
        seeded.clear();
        seeded.setValue(QStringLiteral("tooBig"), 7);
        seeded.setValue(QStringLiteral("goodInt"), 1);
        seeded.sync();
    }

    QTemporaryDir tmp;
    CHECK(tmp.isValid(), "settings temp dir");
    const QString jsonPath = QDir(tmp.path()).filePath(QStringLiteral("bad.mvs"));
    QFile out(jsonPath);
    CHECK(out.open(QIODevice::WriteOnly | QIODevice::Truncate), "open settings json");
    const QByteArray json = QByteArrayLiteral("{\n"
                                              "  \"schema\": \"mviewer.settings\",\n"
                                              "  \"goodInt\": 42,\n"
                                              "  \"tooBig\": 1e20,\n"
                                              "  \"fitsLong\": 3000000000,\n"
                                              "  \"fraction\": 1.5,\n"
                                              "  \"settingsSchemaVersion\": \"abc\"\n"
                                              "}\n");
    CHECK(out.write(json) == static_cast<qint64>(json.size()), "write settings json");
    out.close();

    std::string err;
    const bool imported = mviewer::core::importSettings(jsonPath.toStdString(), &err);
    if (!imported)
        printf("  import error: %s\n", err.c_str());
    CHECK(imported, "import accepts the settings document");

    {
        QSettings restored;
        CHECK(restored.value(QStringLiteral("goodInt")).toInt() == 42,
              "in-range integer is restored");
        CHECK(restored.value(QStringLiteral("tooBig")).toInt() == 7,
              "integer outside int64 is ignored");
        CHECK(restored.value(QStringLiteral("fitsLong")).toLongLong() == 3000000000LL,
              "integer that fits in int64 is restored");
        const double fraction = restored.value(QStringLiteral("fraction")).toDouble();
        CHECK(fraction > 1.49 && fraction < 1.51, "non-integer number is stored as a double");
    }

    mviewer::core::migrateSettingsIfNeeded();
    {
        QSettings migrated;
        bool ok = false;
        const int version = migrated.value(QStringLiteral("settingsSchemaVersion")).toInt(&ok);
        CHECK(ok && version == mviewer::core::kSettingsSchemaVersion,
              "non-numeric schema version is migrated");
    }
}

int main(int argc, char **argv)
{
    QStandardPaths::setTestModeEnabled(true);

    QTemporaryDir isolation;
    if (!isolation.isValid())
    {
        printf("FAIL: isolation temp dir\n");
        return 1;
    }
    const QByteArray home = isolation.path().toUtf8();
    setEnvVar("HOME", home);
#ifdef _WIN32
    setEnvVar("APPDATA", home);
    setEnvVar("LOCALAPPDATA", home);
#endif

    const QString cwdTags = QDir::current().filePath(QStringLiteral("tags.txt"));
    const bool cwdHadTags = QFileInfo::exists(cwdTags);
    mviewer::core::TagStore &tags = mviewer::core::TagStore::instance();
    CHECK(QFileInfo::exists(cwdTags) == cwdHadTags,
          "TagStore::instance before QCoreApplication does not create tags.txt");

    QCoreApplication app(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("mviewer-audit2"));
    QCoreApplication::setApplicationName(QStringLiteral("mviewer-audit2"));
    mviewer::runtime::configureSettings();

    const std::string suffix =
        std::to_string(static_cast<long long>(QCoreApplication::applicationPid()));
    testPresetPaths(suffix);
    testTagPathResolvedAfterApp(tags, cwdHadTags);
    testSettingsRejectBadIntegers();

    printf("\naudit_persistence_tests: %d passed, %d failed\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
