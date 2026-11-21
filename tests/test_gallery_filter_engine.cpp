// Gallery filter engine: matching rules, type filter, and name/path tie-break.

#include "gallery_filter_engine.h"

#include "core/filesystem/Utf8Path.h"

#include <QCoreApplication>
#include <QRegularExpression>
#include <QStringList>

#include <iostream>
#include <string>

namespace
{
int g_failures = 0;

#define CHECK(cond, msg)                                                                           \
    do                                                                                             \
    {                                                                                              \
        if (cond)                                                                                  \
            std::cout << "[ok] " << msg << "\n";                                                   \
        else                                                                                       \
        {                                                                                          \
            std::cout << "[FAIL] " << msg << " (" << __FILE__ << ":" << __LINE__ << ")\n";         \
            ++g_failures;                                                                          \
        }                                                                                          \
    } while (false)

using mviewer::ui::GalleryEntry;
using mviewer::ui::GalleryFilterSnapshot;

GalleryEntry entry(const char *path, const char *name)
{
    GalleryEntry row;
    row.path = QString::fromUtf8(path);
    row.name = QString::fromUtf8(name);
    row.size = 1;
    return row;
}

bool hasPath(const QList<GalleryEntry> &rows, const QString &path)
{
    for (const GalleryEntry &row : rows)
    {
        if (row.path == path)
            return true;
    }
    return false;
}

QString namesOf(const QList<GalleryEntry> &rows)
{
    QStringList names;
    for (const GalleryEntry &row : rows)
        names.append(row.path + QLatin1Char('|') + row.name);
    return names.join(QLatin1Char(','));
}

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);

    const QList<GalleryEntry> source = {
        entry("C:/photos/cat.PNG", "Cat.PNG"),
        entry("C:/photos/dog.jpg", "Dog.jpg"),
        entry("C:/photos/other.png", "other.png"),
    };

    {
        const QList<GalleryEntry> kept = mviewer::ui::evaluateGalleryFilter(source, {});
        CHECK(kept.size() == source.size(), "empty snapshot keeps every entry");
        CHECK(hasPath(kept, source.at(0).path) && hasPath(kept, source.at(1).path) &&
                  hasPath(kept, source.at(2).path),
              "empty snapshot keeps the same paths");
    }

    {
        GalleryFilterSnapshot snapshot;
        snapshot.text = QStringLiteral("dog");
        const QList<GalleryEntry> kept = mviewer::ui::evaluateGalleryFilter(source, snapshot);
        CHECK(kept.size() == 1 && kept.first().name == QStringLiteral("Dog.jpg"),
              "lowercase text matches a name substring");
    }

    {
        GalleryFilterSnapshot snapshot;
        snapshot.text = QStringLiteral("c*.png");
        snapshot.useFuzzy = true;
        const QString pattern =
            QRegularExpression::escape(snapshot.text).replace("\\*", ".*").replace("\\?", ".");
        snapshot.fuzzy.setPattern(pattern);
        snapshot.fuzzy.setPatternOptions(QRegularExpression::CaseInsensitiveOption);
        const QList<GalleryEntry> kept = mviewer::ui::evaluateGalleryFilter(source, snapshot);
        CHECK(kept.size() == 1 && kept.first().name == QStringLiteral("Cat.PNG"),
              "fuzzy regex matches the name");
    }

    {
        GalleryFilterSnapshot snapshot;
        snapshot.text = QStringLiteral("portrait");
        snapshot.query.metadata = true;
        snapshot.metaIndex.insert(mviewer::ui::galleryPathKey(source.at(1).path),
                                  QStringLiteral("canon portrait"));
        const QList<GalleryEntry> kept = mviewer::ui::evaluateGalleryFilter(source, snapshot);
        CHECK(kept.size() == 1 && kept.first().path == source.at(1).path,
              "metadata query reads metaIndex");
    }

    {
        GalleryFilterSnapshot snapshot;
        snapshot.query.recentOnly = true;
        snapshot.recents.insert(mviewer::core::normalizeKey(source.at(2).path.toStdString()));
        const QList<GalleryEntry> kept = mviewer::ui::evaluateGalleryFilter(source, snapshot);
        CHECK(kept.size() == 1 && kept.first().path == source.at(2).path,
              "recentOnly keeps the normalizeKey recent");
    }

    {
        GalleryFilterSnapshot snapshot;
        snapshot.query.camera = "canon";
        snapshot.query.lens = "50mm";
        snapshot.metaCamera.insert(mviewer::ui::galleryPathKey(source.at(0).path),
                                   QStringLiteral("Canon EOS"));
        snapshot.metaLens.insert(mviewer::ui::galleryPathKey(source.at(0).path),
                                 QStringLiteral("EF 50mm"));
        snapshot.metaCamera.insert(mviewer::ui::galleryPathKey(source.at(1).path),
                                   QStringLiteral("Nikon"));
        snapshot.metaLens.insert(mviewer::ui::galleryPathKey(source.at(1).path),
                                 QStringLiteral("EF 50mm"));
        const QList<GalleryEntry> kept = mviewer::ui::evaluateGalleryFilter(source, snapshot);
        CHECK(kept.size() == 1 && kept.first().path == source.at(0).path,
              "camera and lens substrings both match");
    }

    {
        GalleryFilterSnapshot snapshot;
        snapshot.query.iso = 200;
        snapshot.metaIso.insert(mviewer::ui::galleryPathKey(source.at(0).path), 100);
        snapshot.metaIso.insert(mviewer::ui::galleryPathKey(source.at(1).path), 200);
        const QList<GalleryEntry> kept = mviewer::ui::evaluateGalleryFilter(source, snapshot);
        CHECK(kept.size() == 1 && kept.first().path == source.at(1).path, "ISO matches exactly");
    }

    {
        GalleryFilterSnapshot snapshot;
        snapshot.query.type = "png";
        const QList<GalleryEntry> kept = mviewer::ui::evaluateGalleryFilter(source, snapshot);
        CHECK(kept.size() == 2 && hasPath(kept, source.at(0).path) &&
                  hasPath(kept, source.at(2).path) && !hasPath(kept, source.at(1).path),
              "type filter keeps png");
    }

    CHECK(mviewer::ui::galleryPathKey(QStringLiteral("C:\\photos\\a.jpg")) ==
              QStringLiteral("C:/photos/a.jpg"),
          "galleryPathKey turns backslashes into '/'");

    {
        const QList<GalleryEntry> ties = {
            entry("dir/b/same.jpg", "same.jpg"),
            entry("dir/a/same.jpg", "same.jpg"),
            entry("dir/m.jpg", "m.jpg"),
            entry("dir/a.jpg", "a.jpg"),
        };
        GalleryFilterSnapshot ascending;
        ascending.query.sort = mviewer::core::BrowseSortField::Name;
        ascending.query.ascending = true;
        GalleryFilterSnapshot descending = ascending;
        descending.query.ascending = false;
        const QString up = namesOf(mviewer::ui::evaluateGalleryFilter(ties, ascending));
        const QString down = namesOf(mviewer::ui::evaluateGalleryFilter(ties, descending));
        CHECK(up == QStringLiteral("dir/a.jpg|a.jpg,dir/m.jpg|m.jpg,dir/a/same.jpg|same.jpg,"
                                   "dir/b/same.jpg|same.jpg"),
              "ascending name order tie-breaks equal names by path");
        CHECK(down == QStringLiteral("dir/a/same.jpg|same.jpg,dir/b/same.jpg|same.jpg,"
                                     "dir/m.jpg|m.jpg,dir/a.jpg|a.jpg"),
              "descending name order still tie-breaks equal names by path");
    }

    return g_failures == 0 ? 0 : 1;
}
