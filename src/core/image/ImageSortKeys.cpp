#include "core/image/ImageSortKeys.h"

#include "core/RatingStore.h"
#include "core/image/RawMetadata.h"

#include <QFileInfo>
#include <QImageReader>
#include <QString>
#include <QSize>

#include <algorithm>
#include <cctype>
#include <string>

namespace mviewer::core
{

namespace
{

std::string lower(std::string s)
{
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

RawMetadata defaultMetaReader(const std::string &path)
{
    return parseRawMetadata(path);
}

} // namespace

std::vector<ImageSortKey> computeSortKeys(const std::vector<std::string> &paths, SortField field,
                                          const SortDimensionReader &dimensionReader,
                                          const SortMetadataReader &metadataReader)
{
    std::vector<ImageSortKey> keys;
    keys.reserve(paths.size());

    const SortDimensionReader dims =
        dimensionReader ? dimensionReader
                        : [](const std::string &p) -> int64_t
    {
        QImageReader reader(QString::fromStdString(p));
        reader.setAutoTransform(true);
        const QSize s = reader.size();
        return static_cast<int64_t>(s.width()) * s.height();
    };
    const SortMetadataReader metas =
        metadataReader ? metadataReader : [](const std::string &p) { return defaultMetaReader(p); };

    // Which per-file sources does this field need? Keeps a name/type sort
    // completely free of header/metadata I/O.
    const bool needFileInfo = (field == SortField::Date || field == SortField::Size);
    const bool needResolution = (field == SortField::Resolution);
    const bool needCameraLens = (field == SortField::Camera || field == SortField::Lens);
    const bool needRating = (field == SortField::Rating);

    for (const std::string &p : paths)
    {
        ImageSortKey k;
        k.path = p;
        if (needFileInfo)
        {
            const QFileInfo fi(QString::fromStdString(p));
            k.size = fi.size();
            k.mtimeSec = fi.lastModified().toSecsSinceEpoch();
        }
        if (field == SortField::Type || field == SortField::Name)
        {
            const QFileInfo fi(QString::fromStdString(p));
            k.suffix = lower(fi.suffix().toStdString());
        }
        if (needResolution)
            k.resolution = dims(p);
        if (needCameraLens)
        {
            const RawMetadata rm = metas(p);
            if (field == SortField::Camera)
            {
                std::string cam = rm.make;
                if (!rm.model.empty())
                {
                    if (!cam.empty())
                        cam += " ";
                    cam += rm.model;
                }
                k.camera = lower(cam);
            }
            else
                k.lens = lower(rm.lens);
        }
        if (needRating)
            k.rating = RatingStore::instance().rating(p);
        keys.push_back(std::move(k));
    }
    return keys;
}

} // namespace mviewer::core
