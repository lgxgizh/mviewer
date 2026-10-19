//
// Copyright (c) 2026 mviewer project. All rights reserved.
// SPDX-License-Identifier: MIT
//
// P1: unit tests for the persistent star-rating store.
//
#include "core/RatingStore.h"
#include "core/SidecarStore.h"
#include "core/TagStore.h"
#include "core/command/FileDeleteCommand.h"
#include "core/command/FileMoveCommand.h"
#include "core/command/FileRenameCommand.h"
#include "core/filesystem/Utf8Path.h"

#include <QDir>
#include <QFile>
#include <QImage>
#include <QTemporaryDir>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
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

using namespace mviewer::core;

static std::string writeImage(const QString &dir, const char *name)
{
    QImage img(4, 4, QImage::Format_RGB32);
    img.fill(Qt::blue);
    const QString path = QDir(dir).filePath(QString::fromUtf8(name));
    img.save(path, "PNG");
    return path.toUtf8().toStdString();
}

static bool fileExists(const std::string &path)
{
    std::error_code ec;
    return std::filesystem::exists(mviewer::core::pathFromUtf8(path), ec);
}

static void testPathKeyAndAtomicRelocate()
{
    using namespace mviewer::core;
    printf("\n[path key case-fold + atomic sidecar relocate]\n");
    QTemporaryDir tmp;
    auto &rs = RatingStore::instance();
    auto &ss = SidecarStore::instance();
    rs.setFilePath(QDir(tmp.path()).filePath("case_ratings.txt").toUtf8().toStdString());

    const std::string mixed = "C:/Photos/Shot.JPG";
    const std::string lower = "c:/photos/shot.jpg";
    const std::string backslash = "C:\\Photos\\Shot.JPG";
    CHECK(normalizePathKey(mixed) == normalizePathKey("C:/Photos/Shot.JPG"),
          "normalizePathKey is stable for forward slashes");
    CHECK(normalizePathKey(backslash) == normalizePathKey(mixed),
          "normalizePathKey folds backslashes to forward slashes");
#ifdef _WIN32
    CHECK(normalizePathKey(mixed) == normalizePathKey(lower),
          "Windows path keys are case-insensitive");
    rs.setRating(mixed, 5);
    CHECK(rs.rating(lower) == 5, "Windows RatingStore lookup ignores case");
    rs.clearRating(mixed);
#else
    CHECK(normalizePathKey(mixed) != normalizePathKey(lower),
          "non-Windows path keys stay case-sensitive");
#endif

    const QString dir = tmp.path();
    const std::string oldPath = writeImage(dir, "atomic_old.png");
    const std::string newPath = QDir(dir).filePath("atomic_new.png").toUtf8().toStdString();
    rs.setRating(oldPath, 4);
    rs.setPicked(oldPath, true);
    CHECK(ss.writeSidecar(oldPath), "atomic relocate: source sidecar written");
    const std::string oldSidecar = SidecarStore::sidecarPath(oldPath);
    CHECK(ss.relocateSidecar(oldPath, newPath), "atomic relocate: relocateSidecar succeeds");
    CHECK(fileExists(SidecarStore::sidecarPath(newPath)),
          "atomic relocate: destination sidecar exists after write");
    CHECK(!fileExists(oldSidecar), "atomic relocate: source sidecar removed only after write");
    rs.clearRating(newPath);
    rs.setPicked(newPath, false);
    CHECK(ss.readSidecar(newPath) && rs.rating(newPath) == 4 && rs.picked(newPath),
          "atomic relocate: destination sidecar restores rating and pick");
    rs.clearRating(newPath);
    rs.setPicked(newPath, false);
    rs.flushSave();
}

static void testMetadataFollowsFile()
{
    using namespace mviewer::core;
    printf("\n[metadata follows rename / move / delete and undo]\n");
    QTemporaryDir tmp;
    auto &rs = RatingStore::instance();
    auto &ts = TagStore::instance();
    auto &ss = SidecarStore::instance();
    rs.setFilePath(QDir(tmp.path()).filePath("follow_ratings.txt").toUtf8().toStdString());
    ts.setFilePath(QDir(tmp.path()).filePath("follow_tags.txt").toUtf8().toStdString());
    const QString dir = tmp.path();
    const std::string a = writeImage(dir, "a.png");
    const std::string renamed = QDir(dir).filePath("b.png").toUtf8().toStdString();
    rs.setRating(a, 4);
    rs.setColorLabel(a, 3);
    rs.setPicked(a, true);
    ts.addTag(a, "keep");
    CHECK(ss.writeSidecar(a), "sidecar written before rename");

    FileRenameCommand rename(a, renamed);
    rename.execute();
    CHECK(rs.rating(renamed) == 4 && rs.colorLabel(renamed) == 3 && rs.picked(renamed),
          "rename moves rating, label and pick to the new path");
    CHECK(rs.rating(a) == 0 && !rs.picked(a), "rename leaves nothing on the old path");
    CHECK(ts.hasTag(renamed, "keep") && !ts.hasTag(a, "keep"), "rename moves tags");
    CHECK(fileExists(SidecarStore::sidecarPath(renamed)) &&
              !fileExists(SidecarStore::sidecarPath(a)),
          "rename moves the sidecar");
    rename.undo();
    CHECK(rs.rating(a) == 4 && ts.hasTag(a, "keep") && fileExists(SidecarStore::sidecarPath(a)),
          "undo rename moves metadata and sidecar back");

    const std::string sub = QDir(dir).filePath("sub").toUtf8().toStdString();
    FileMoveCommand move({a}, sub);
    move.execute();
    const std::string moved = move.moved().empty() ? std::string{} : move.moved()[0].second;
    CHECK(!moved.empty() && rs.rating(moved) == 4 && ts.hasTag(moved, "keep") &&
              fileExists(SidecarStore::sidecarPath(moved)),
          "move carries metadata and sidecar to the destination");
    move.undo();
    CHECK(rs.rating(a) == 4 && rs.rating(moved) == 0, "undo move restores metadata");

    const std::string trash = QDir(dir).filePath("trash").toUtf8().toStdString();
    FileDeleteCommand del({a}, trash);
    del.execute();
    CHECK(rs.rating(a) == 0, "delete takes metadata off the deleted path");
    del.undo();
    CHECK(rs.rating(a) == 4 && rs.colorLabel(a) == 3 && rs.picked(a) && ts.hasTag(a, "keep") &&
              fileExists(SidecarStore::sidecarPath(a)),
          "undo delete restores rating, label, pick, tags and sidecar");

    // H2: same stem, different extension -> separate sidecars.
    const std::string jpg = writeImage(dir, "IMG_001.jpg");
    const std::string png = writeImage(dir, "IMG_001.png");
    CHECK(SidecarStore::sidecarPath(jpg) != SidecarStore::sidecarPath(png),
          "IMG_001.jpg and IMG_001.png get distinct sidecars");
    rs.setRating(jpg, 2);
    rs.setRating(png, 5);
    ss.writeSidecar(jpg);
    ss.writeSidecar(png);
    rs.clearRating(jpg);
    rs.clearRating(png);
    ss.readSidecar(jpg);
    ss.readSidecar(png);
    CHECK(rs.rating(jpg) == 2 && rs.rating(png) == 5, "same-stem sidecars do not overwrite");

    // Legacy stem.xmp: migrated when it names its image, ignored when ambiguous.
    const std::string solo = writeImage(dir, "solo.png");
    const std::string legacySolo = QDir(dir).filePath("solo.xmp").toUtf8().toStdString();
    std::ofstream(pathFromUtf8(legacySolo)) << "{ \"rating\": 3 }\n";
    CHECK(ss.readSidecar(solo) && rs.rating(solo) == 3, "unambiguous legacy sidecar is read");
    CHECK(!fileExists(legacySolo) && fileExists(SidecarStore::sidecarPath(solo)),
          "legacy sidecar is migrated to the per-file name");
    const std::string legacyShared = QDir(dir).filePath("IMG_001.xmp").toUtf8().toStdString();
    std::ofstream(pathFromUtf8(legacyShared)) << "{ \"rating\": 1 }\n";
    std::filesystem::remove(pathFromUtf8(SidecarStore::sidecarPath(jpg)));
    rs.clearRating(jpg);
    ss.readSidecar(jpg);
    CHECK(rs.rating(jpg) == 0 && fileExists(legacyShared),
          "ambiguous legacy sidecar (two images share the stem) is not applied");
    rs.clearRating(png);
    rs.clearRating(solo);
    rs.setPicked(a, false);
    rs.clearRating(a);
    rs.clearColorLabel(a);
    ts.clearTags(a);
    // Flush now: a debounced write still pending at process exit would race
    // static destruction.
    rs.flushSave();
    ts.flushSave();
}

static QByteArray readFileBytes(const std::string &path)
{
    QFile file(QString::fromStdString(path));
    if (!file.open(QIODevice::ReadOnly))
        return {};
    return file.readAll();
}

// Audit #42 / #44 / #45: new sidecars are .mviewer.json, real XMP is left
// alone, CRLF store files share normalizeKey with lookup, and tag text that
// contains the record separators is rejected. save() rewrites the file even
// after the dirty bit has been cleared.
static void testAuditSidecarAndKeyNormalization()
{
    printf("\n[audit sidecar suffix, CRLF keys, tag separators]\n");
    auto &rs = RatingStore::instance();
    auto &ss = SidecarStore::instance();
    auto &ts = TagStore::instance();

    QTemporaryDir tmp;
    CHECK(tmp.isValid(), "audit temp dir");
    const QString dir = tmp.path();

    const std::string fresh = writeImage(dir, "fresh.png");
    rs.setRating(fresh, 3);
    CHECK(ss.writeSidecar(fresh), "sidecar write uses the new suffix");
    CHECK(SidecarStore::sidecarPath(fresh).ends_with(".mviewer.json"),
          "sidecarPath ends with .mviewer.json");
    CHECK(fileExists(SidecarStore::sidecarPath(fresh)), "sidecar file is .mviewer.json");
    CHECK(!fileExists(fresh + ".xmp"), "new writes do not create .xmp");

    const std::string third = writeImage(dir, "third.jpg");
    const std::string thirdXmp = third + ".xmp";
    const QByteArray xmpPacket =
        QByteArrayLiteral("<?xpacket begin=\"\" id=\"W5M0MpCehiHzreSzNTczkc9d\"?>\n"
                          "<x:xmpmeta xmlns:x=\"adobe:ns:meta/\">\n"
                          " <rdf:RDF xmlns:rdf=\"http://www.w3.org/1999/02/22-rdf-syntax-ns#\">\n"
                          " </rdf:RDF>\n"
                          "</x:xmpmeta>\n"
                          "<?xpacket end=\"w\"?>\n");
    {
        QFile xmpFile(QString::fromStdString(thirdXmp));
        CHECK(xmpFile.open(QIODevice::WriteOnly | QIODevice::Truncate), "write third-party XMP");
        CHECK(xmpFile.write(xmpPacket) == static_cast<qint64>(xmpPacket.size()),
              "third-party XMP bytes stored");
    }
    const QByteArray xmpBefore = readFileBytes(thirdXmp);
    CHECK(xmpBefore.contains("x:xmpmeta"), "fixture is real XMP");
    CHECK(!ss.readSidecar(third), "real XMP is not a readable sidecar");
    CHECK(rs.rating(third) == 0, "real XMP does not apply a rating");
    CHECK(readFileBytes(thirdXmp) == xmpBefore, "reading past real XMP does not change it");

    rs.setRating(third, 4);
    CHECK(ss.writeSidecar(third), "rating write leaves real XMP in place");
    CHECK(fileExists(SidecarStore::sidecarPath(third)), "rating landed in .mviewer.json");
    CHECK(readFileBytes(thirdXmp) == xmpBefore, "write does not modify real XMP");
    rs.clearRating(third);
    CHECK(ss.readSidecar(third) && rs.rating(third) == 4, "read restores the .mviewer.json rating");
    CHECK(readFileBytes(thirdXmp) == xmpBefore, "read does not rewrite real XMP");
    CHECK(ss.removeSidecar(third), "remove sidecar");
    CHECK(!fileExists(SidecarStore::sidecarPath(third)), ".mviewer.json removed");
    CHECK(readFileBytes(thirdXmp) == xmpBefore, "remove does not delete real XMP");

    const std::string legacy = writeImage(dir, "legacy.jpg");
    const std::string legacyXmp = legacy + ".xmp";
    {
        QFile legacyFile(QString::fromStdString(legacyXmp));
        const QByteArray body = QByteArrayLiteral("{ \"rating\": 2 }\n");
        CHECK(legacyFile.open(QIODevice::WriteOnly | QIODevice::Truncate), "open legacy json xmp");
        CHECK(legacyFile.write(body) == static_cast<qint64>(body.size()), "write legacy json xmp");
    }
    rs.clearRating(legacy);
    CHECK(ss.readSidecar(legacy) && rs.rating(legacy) == 2, "legacy JSON .xmp is read");
    CHECK(fileExists(SidecarStore::sidecarPath(legacy)),
          "legacy sidecar migrated to .mviewer.json");
    CHECK(!fileExists(legacyXmp), "legacy JSON .xmp removed after migration");

    {
        QTemporaryDir importDir;
        CHECK(importDir.isValid(), "import temp dir");
        const std::string one = writeImage(importDir.path(), "one.png");
        const std::string other = writeImage(importDir.path(), "other.png");
        rs.setRating(one, 5);
        CHECK(ss.writeSidecar(one), "import fixture sidecar");
        rs.clearRating(one);
        {
            QFile otherXmp(QString::fromStdString(other + ".xmp"));
            const QByteArray packet = QByteArrayLiteral("<x:xmpmeta></x:xmpmeta>\n");
            CHECK(otherXmp.open(QIODevice::WriteOnly | QIODevice::Truncate), "open untouched xmp");
            CHECK(otherXmp.write(packet) == static_cast<qint64>(packet.size()),
                  "write untouched xmp");
        }
        const QByteArray otherBefore = readFileBytes(other + ".xmp");
        CHECK(ss.importDirectory(importDir.path().toStdString()) == 1,
              "importDirectory reads .mviewer.json and skips real XMP");
        CHECK(rs.rating(one) == 5, "import restores the sidecar rating");
        CHECK(rs.rating(other) == 0, "import ignores a third-party XMP");
        CHECK(readFileBytes(other + ".xmp") == otherBefore, "import leaves third-party XMP bytes");
        rs.clearRating(one);
        ss.removeSidecar(one);
    }

    QTemporaryDir keys;
    CHECK(keys.isValid(), "key normalization temp dir");
    const QString ratingsPath = QDir(keys.path()).filePath(QStringLiteral("ratings.txt"));
    {
        QFile ratings(ratingsPath);
        CHECK(ratings.open(QIODevice::WriteOnly | QIODevice::Truncate), "open crlf ratings");
        const QByteArray line("4|C:\\Photos\\..\\Photos\\a.jpg\r\n");
        CHECK(ratings.write(line) == static_cast<qint64>(line.size()), "write crlf ratings");
    }
    rs.setFilePath(ratingsPath.toStdString());
    CHECK(rs.rating("C:/Photos/a.jpg") == 4, "CRLF rating reloads through normalizeKey");
    const std::string folded = normalizeKey("C:\\Photos\\..\\Photos\\a.jpg");
    CHECK(folded.find('\\') == std::string::npos, "normalizeKey folds backslashes");
    CHECK(folded.find("..") == std::string::npos, "normalizeKey drops dot-dot segments");
#ifdef _WIN32
    CHECK(folded == normalizeKey("c:/photos/a.jpg"), "Windows keys compare case-insensitively");
#endif

    const QString tagsPath = QDir(keys.path()).filePath(QStringLiteral("tags.txt"));
    {
        QFile tagsFile(tagsPath);
        CHECK(tagsFile.open(QIODevice::WriteOnly | QIODevice::Truncate), "open crlf tags");
        const QByteArray line("keep|C:\\Photos\\..\\Photos\\a.jpg\r\n");
        CHECK(tagsFile.write(line) == static_cast<qint64>(line.size()), "write crlf tags");
    }
    ts.setFilePath(tagsPath.toStdString());
    const std::string photo = "C:/Photos/a.jpg";
    CHECK(ts.hasTag(photo, "keep"), "CRLF tag reloads through normalizeKey");
    ts.addTag(photo, "a|b");
    ts.addTag(photo, "a\nb");
    CHECK(!ts.hasTag(photo, "a|b"), "tag containing '|' is rejected");
    CHECK(!ts.hasTag(photo, "a\nb"), "tag containing a newline is rejected");
    ts.setTags(photo, {"a|b", "a\nb"});
    CHECK(ts.hasTag(photo, "keep"), "rejecting every tag does not clear stored tags");
    CHECK(ts.save(), "save persists the kept tag");
    {
        QFile tagsFile(tagsPath);
        CHECK(tagsFile.open(QIODevice::WriteOnly | QIODevice::Truncate), "corrupt tags file");
        const QByteArray stale("CORRUPT\n");
        CHECK(tagsFile.write(stale) == static_cast<qint64>(stale.size()), "stale tags written");
    }
    CHECK(ts.save(), "save rewrites even when the dirty flag is clear");
    const QString saved = QString::fromUtf8(readFileBytes(tagsPath.toStdString()));
    CHECK(saved.contains(QStringLiteral("keep|")), "second save wrote the tag");
    CHECK(!saved.contains(QStringLiteral("a|b")), "rejected pipe tag was not written");
    CHECK(!saved.contains(QStringLiteral("CORRUPT")), "stale tags file was replaced");
    CHECK(!saved.contains(QStringLiteral("a\nb")), "rejected newline tag was not written");
    ts.setTags(photo, {});
    CHECK(!ts.hasTag(photo, "keep"), "empty setTags clears tags");
    ts.flushSave();
    rs.flushSave();
}

int main()
{
    auto &s = RatingStore::instance();

    // Keep every ratings/flags pair under unique temp dirs so parallel ctest
    // (-jN) cannot clobber a shared ./flags.txt next to relative paths.
    QTemporaryDir suiteTmp;
    if (!suiteTmp.isValid())
    {
        printf("FAIL: suite temp dir\n");
        return 1;
    }
    const std::string suiteDir = suiteTmp.path().toUtf8().toStdString();
    s.setFilePath(suiteDir + "/test_ratings_tmp.txt");

    // Regression: recents are worker-debounced and must persist even when no
    // later setFilePath()/destructor flush is available to hide a broken timer.
    const std::filesystem::path recentDir =
        std::filesystem::temp_directory_path() / "mviewer_ratingstore_recent_test";
    std::error_code recentEc;
    std::filesystem::create_directories(recentDir, recentEc);
    const auto recentRatings = recentDir / "ratings.txt";
    const auto recentFlags = recentDir / "flags.txt";
    std::filesystem::remove(recentRatings, recentEc);
    std::filesystem::remove(recentFlags, recentEc);
    s.setFilePath(recentRatings.string());
    s.addRecent("debounced.png");
    std::this_thread::sleep_for(std::chrono::milliseconds(250));
    bool recentWritten = false;
    {
        std::ifstream flags(recentFlags);
        std::string line;
        while (std::getline(flags, line))
            recentWritten = recentWritten || line == "N|debounced.png";
    }
    CHECK(recentWritten, "recent is written after the debounce quiet period");
    s.setFilePath(recentRatings.string());
    CHECK(!s.recents().empty() && s.recents().front() == "debounced.png",
          "debounced recent reloads from disk");

    s.setRating("a.jpg", 3);
    CHECK(s.rating("a.jpg") == 3, "rating set to 3");
    s.setRating("C:\\photos\\rated.png", 4);
    s.setColorLabel("C:\\photos\\rated.png", 2);
    s.setRejected("C:\\photos\\rated.png", true);
    s.setPicked("C:/photos/picked.png", true);
    s.addRecent("C:\\photos\\recent.png");
    {
        const RatingStore::Snapshot snap = s.snapshot();
        CHECK(snap.rating("C:/photos/rated.png") == 4, "snapshot rating ignores slash style");
        CHECK(snap.colorLabel("C:/photos/rated.png") == 2,
              "snapshot color label ignores slash style");
        CHECK(snap.isRejected("C:/photos/rated.png"), "snapshot reject ignores slash style");
        CHECK(snap.isPicked("C:\\photos\\picked.png"), "snapshot pick ignores slash style");
        CHECK(snap.isRecent("C:/photos/recent.png"), "snapshot recent ignores slash style");
    }
    s.clearRating("C:\\photos\\rated.png");
    s.clearColorLabel("C:/photos/rated.png");
    s.setRejected("C:/photos/rated.png", false);
    s.setPicked("C:\\photos\\picked.png", false);
    CHECK(s.hasRating("a.jpg"), "hasRating true after set");

    s.setRating("a.jpg", 9); // clamps to 5
    CHECK(s.rating("a.jpg") == 5, "rating clamps to 5");

    s.setRating("a.jpg", -1); // clamps to 0 -> cleared
    CHECK(s.rating("a.jpg") == 0, "negative rating clamps to 0 (cleared)");
    CHECK(!s.hasRating("a.jpg"), "hasRating false after clear");

    // Persistence: save to file A, then reload from A after pointing elsewhere.
    s.setFilePath(suiteDir + "/test_ratings_a.txt");
    s.setRating("persist.png", 4);
    CHECK(s.save(), "save() returns true");

    // Simulate losing in-memory state by pointing at a different (empty) file,
    // then reloading from the original file that holds the persisted rating.
    s.setFilePath(suiteDir + "/test_ratings_b.txt");
    CHECK(!s.load(), "load() of a missing file returns false");
    CHECK(s.rating("persist.png") == 0, "rating absent from empty file B");
    s.setFilePath(suiteDir + "/test_ratings_a.txt");
    CHECK(s.load(), "load() returns true");
    CHECK(s.rating("persist.png") == 4, "persisted rating reloaded from disk");

    // M49 Windows contract: a user path is UTF-8 at the core boundary and is
    // converted to native filesystem paths only at the I/O edge. Include
    // spaces, CJK, and an emoji in both directory and filename.
    const QString unicodeDir =
        QDir(QDir::tempPath()).filePath(QStringLiteral("mviewer_path closure 😀/nested directory"));
    QDir().mkpath(unicodeDir);
    const QString unicodeImage = QDir(unicodeDir).filePath(QStringLiteral("test image 😀.png"));
    QImage unicodeFixture(8, 8, QImage::Format_RGB32);
    unicodeFixture.fill(Qt::blue);
    CHECK(unicodeFixture.save(unicodeImage, "PNG"), "Unicode fixture is written");
    const std::string unicodePath = unicodeImage.toUtf8().toStdString();
    const std::string unicodeRatings =
        QDir(unicodeDir).filePath(QStringLiteral("rating status.txt")).toUtf8().toStdString();
    const std::string roundTrip = pathToUtf8(pathFromUtf8(unicodePath));
    CHECK(roundTrip == unicodePath, "UTF-8 path round-trips without locale loss");
    s.setFilePath(unicodeRatings);
    s.setRating(unicodePath, 5);
    s.setColorLabel(unicodePath, 4);
    s.setPicked(unicodePath, true);
    const std::string unicodeSidecar = SidecarStore::sidecarPath(unicodePath);
    CHECK(unicodeSidecar.find("test image") != std::string::npos,
          "sidecar identity keeps the Unicode filename");
    CHECK(SidecarStore::instance().writeSidecar(unicodePath), "Unicode sidecar write succeeds");
    const QString sidecarPath =
        QString::fromUtf8(unicodeSidecar.data(), static_cast<int>(unicodeSidecar.size()));
    CHECK(QFileInfo::exists(sidecarPath), "Unicode sidecar exists at the native path");
    s.clearRating(unicodePath);
    s.clearColorLabel(unicodePath);
    s.setPicked(unicodePath, false);
    CHECK(SidecarStore::instance().readSidecar(unicodePath), "Unicode sidecar read succeeds");
    CHECK(s.rating(unicodePath) == 5 && s.colorLabel(unicodePath) == 4 && s.picked(unicodePath),
          "Unicode sidecar restores RatingStore identity");
    CHECK(SidecarStore::instance().removeSidecar(unicodePath), "Unicode sidecar remove succeeds");
    CHECK(!QFileInfo::exists(sidecarPath), "Unicode sidecar removal reaches the native file");
    CHECK(!SidecarStore::instance().readSidecar(unicodePath + ".missing"),
          "missing Unicode sidecar is a handled failure");
    QDir(QDir(QDir::tempPath()).filePath(QStringLiteral("mviewer_path closure 😀")))
        .removeRecursively();

#ifdef _WIN32
    const std::string uncForward = "//myserver/photos/test.png";
    const std::filesystem::path uncPath = pathFromUtf8(uncForward);
    CHECK(uncPath.wstring() == L"\\\\myserver\\photos\\test.png",
          "pathFromUtf8 normalizes forward-slash UNC to Windows backslash UNC");
    CHECK(normalizePathKey(uncForward) == "//myserver/photos/test.png",
          "normalizePathKey preserves forward-slash format for UNC keys");
#endif

    std::filesystem::remove(recentRatings, recentEc);
    std::filesystem::remove(recentFlags, recentEc);

    // Prune missing entries for RatingStore and TagStore
    {
        QTemporaryDir pruneTmp;
        const QString existFile = pruneTmp.filePath("real_image.png");
        QImage dummy(4, 4, QImage::Format_RGB32);
        dummy.fill(Qt::red);
        CHECK(dummy.save(existFile, "PNG"), "Created real image for prune test");
        const std::string existPath = existFile.toUtf8().toStdString();
        const std::string missingPath =
            pruneTmp.filePath("no_such_file.png").toUtf8().toStdString();

        s.setRating(existPath, 4);
        s.setColorLabel(existPath, 2);
        s.setPicked(existPath, true);
        s.setRating(missingPath, 3);
        s.setColorLabel(missingPath, 1);
        s.setPicked(missingPath, true);

        CHECK(s.hasRating(missingPath), "missingPath has rating before prune");
        CHECK(s.hasColorLabel(missingPath), "missingPath has label before prune");
        CHECK(s.picked(missingPath), "missingPath is picked before prune");

        size_t pruned = s.pruneMissing();
        CHECK(pruned >= 3, "pruneMissing removed missingPath entries");
        CHECK(!s.hasRating(missingPath), "missingPath rating pruned");
        CHECK(!s.hasColorLabel(missingPath), "missingPath label pruned");
        CHECK(!s.picked(missingPath), "missingPath picked flag pruned");
        CHECK(s.rating(existPath) == 4, "existPath rating preserved");
        CHECK(s.colorLabel(existPath) == 2, "existPath label preserved");
        CHECK(s.picked(existPath), "existPath picked preserved");

        // TagStore pruning
        auto &ts = TagStore::instance();
        ts.setFilePath(pruneTmp.path().toUtf8().toStdString() + "/test_tags.txt");
        ts.addTag(existPath, "landscape");
        ts.addTag(missingPath, "ghost");
        CHECK(ts.hasTag(missingPath, "ghost"), "TagStore has tag for missingPath before prune");
        size_t tagsPruned = ts.pruneMissing();
        CHECK(tagsPruned >= 1, "TagStore pruneMissing pruned ghost tag");
        CHECK(!ts.hasTag(missingPath, "ghost"), "TagStore ghost tag gone");
        CHECK(ts.hasTag(existPath, "landscape"), "TagStore landscape tag preserved");
    }

    testPathKeyAndAtomicRelocate();
    testMetadataFollowsFile();
    testAuditSidecarAndKeyNormalization();

    printf("\nratingstore_tests: %d passed, %d failed\n", g_pass, g_fail);
    return g_fail;
}
