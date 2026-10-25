#include "metadatamodel.h"

#include "rawsourcebadge.h"

#include <QCoreApplication>
#include <QDateTime>

namespace
{
QString toQString(const std::string &s)
{
    return QString::fromStdString(s);
}

QString ratioText(int w, int h)
{
    if (w <= 0 || h <= 0)
        return QString();
    int a = w, b = h;
    while (b)
    {
        const int t = a % b;
        a = b;
        b = t;
    }
    const int g = a;
    return QString("%1:%2").arg(w / g).arg(h / g);
}

// Core ICC parsing reports English names; listing them here keeps them in the
// "IccProfile" translation context so the metadata view can localize them.
[[maybe_unused]] constexpr const char *kIccProfileNames[] = {
    QT_TRANSLATE_NOOP("IccProfile", "Scanner"),
    QT_TRANSLATE_NOOP("IccProfile", "Display"),
    QT_TRANSLATE_NOOP("IccProfile", "Printer"),
    QT_TRANSLATE_NOOP("IccProfile", "Device Link"),
    QT_TRANSLATE_NOOP("IccProfile", "Abstract"),
    QT_TRANSLATE_NOOP("IccProfile", "Color Space"),
    QT_TRANSLATE_NOOP("IccProfile", "Named Color"),
    QT_TRANSLATE_NOOP("IccProfile", "Camera"),
    QT_TRANSLATE_NOOP("IccProfile", "Video"),
    QT_TRANSLATE_NOOP("IccProfile", "Gray"),
    QT_TRANSLATE_NOOP("IccProfile", "Perceptual"),
    QT_TRANSLATE_NOOP("IccProfile", "Media-Relative Colorimetric"),
    QT_TRANSLATE_NOOP("IccProfile", "Saturation"),
    QT_TRANSLATE_NOOP("IccProfile", "ICC-Absolute Colorimetric"),
    QT_TRANSLATE_NOOP("IccProfile", "Unknown"),
};

QString iccText(const std::string &value)
{
    return QCoreApplication::translate("IccProfile", value.c_str());
}

QString orientationText(int o)
{
    switch (o)
    {
    case 1:
        return QCoreApplication::translate("MetadataModel", "Normal (1)");
    case 2:
        return QCoreApplication::translate("MetadataModel", "Flip Horizontal (2)");
    case 3:
        return QCoreApplication::translate("MetadataModel", "Rotate 180° (3)");
    case 4:
        return QCoreApplication::translate("MetadataModel", "Flip Vertical (4)");
    case 5:
        return QCoreApplication::translate("MetadataModel", "Transpose 90° (5)");
    case 6:
        return QCoreApplication::translate("MetadataModel", "Rotate 90° CW (6)");
    case 7:
        return QCoreApplication::translate("MetadataModel", "Transverse 90° (7)");
    case 8:
        return QCoreApplication::translate("MetadataModel", "Rotate 90° CCW (8)");
    default:
        return QCoreApplication::translate("MetadataModel", "Unknown (%1)").arg(o);
    }
}

QString colorSpaceText(const QString &cs)
{
    if (cs == "sRGB")
        return QString("sRGB");
    if (cs == "AdobeRGB")
        return QString("Adobe RGB");
    if (cs == "DisplayP3")
        return QString("Display P3");
    if (cs == "ProPhoto")
        return QString("ProPhoto RGB");
    if (cs.isEmpty())
        return QCoreApplication::translate("MetadataModel", "Not embedded");
    return cs;
}
} // namespace

MetadataModel::MetadataModel(QObject *parent) : QAbstractItemModel(parent)
{
    clearNodes();
}

MetadataModel::~MetadataModel() = default;

void MetadataModel::clearNodes()
{
    qDeleteAll(m_root.children);
    m_root.children.clear();
}

void MetadataModel::setImage(const mviewer::domain::ImageMetadata &meta)
{
    m_meta = meta;
    rebuild();
}

void MetadataModel::setRaw(const mviewer::core::RawMetadata &rm)
{
    m_raw = rm;
    rebuild();
}

void MetadataModel::clear()
{
    m_meta = mviewer::domain::ImageMetadata{};
    m_raw = mviewer::core::RawMetadata{};
    rebuild();
}

MetadataModel::Node *MetadataModel::addCategory(const QString &title)
{
    auto *cat = new Node;
    cat->isCategory = true;
    cat->key = title;
    cat->parent = &m_root;
    m_root.children.append(cat);
    return cat;
}

void MetadataModel::addLeaf(Node *cat, const QString &key, const QString &value)
{
    if (!cat)
        return;
    auto *leaf = new Node;
    leaf->key = key;
    leaf->value = value;
    leaf->parent = cat;
    cat->children.append(leaf);
}

void MetadataModel::rebuild()
{
    beginResetModel();
    clearNodes();

    const bool hasFile = !m_meta.filePath.empty() || !m_meta.fileName.empty();
    if (!hasFile && !m_raw.parsed)
    {
        auto *cat = addCategory(tr("Hint"));
        addLeaf(cat, tr("Description"), tr("Select an image in the gallery to view metadata"));
        endResetModel();
        return;
    }

    buildFileSection();
    buildImageSection();
    buildTextSection();
    buildIccSection();
    buildRawSection();
    endResetModel();
}

void MetadataModel::buildFileSection()
{
    auto *file = addCategory(tr("File information"));
    addLeaf(file, tr("File Name"), toQString(m_meta.fileName));
    addLeaf(file, tr("Path"), toQString(m_meta.filePath));
    addLeaf(file, tr("Size"),
            m_meta.fileSize > 0 ? tr("%1 bytes").arg(m_meta.fileSize) : tr("Unknown"));
    if (m_meta.modifiedEpochSec > 0)
    {
        QDateTime t;
        t.setSecsSinceEpoch(static_cast<qint64>(m_meta.modifiedEpochSec));
        addLeaf(file, tr("Modified"), t.toString("yyyy-MM-dd HH:mm:ss"));
    }
}

void MetadataModel::buildImageSection()
{
    auto *img = addCategory(tr("Image Information"));
    addLeaf(img, tr("Format"), toQString(m_meta.format));
    if (m_meta.width > 0 && m_meta.height > 0)
    {
        const double mp = m_meta.width * m_meta.height / 1000000.0;
        addLeaf(img, tr("Dimensions"), tr("%1 × %2 pixels").arg(m_meta.width).arg(m_meta.height));
        addLeaf(img, tr("Aspect Ratio"), ratioText(m_meta.width, m_meta.height));
        addLeaf(img, tr("Megapixels"), QString("%1 MP").arg(mp, 0, 'f', 2));
    }
    addLeaf(img, tr("Bit Depth"),
            m_meta.bitDepth > 0 ? tr("%1-bit").arg(m_meta.bitDepth) : tr("Unknown"));
    addLeaf(img, tr("Channels"),
            m_meta.channels > 0 ? QString::number(m_meta.channels) : tr("Unknown"));
    addLeaf(img, tr("Color Space"), colorSpaceText(toQString(m_meta.colorSpace)));
    if (m_meta.dpiX > 0 || m_meta.dpiY > 0)
        addLeaf(img, tr("Resolution (DPI)"), QString("%1 × %2").arg(m_meta.dpiX).arg(m_meta.dpiY));
    addLeaf(img, tr("Orientation"), orientationText(m_meta.orientation));
    addLeaf(img, tr("ICC Profile"), m_meta.hasIccProfile ? tr("Embedded") : tr("None"));
    if (m_meta.rawSource != mviewer::domain::RawSource::NotRaw)
    {
        addLeaf(img, tr("Display source"),
                rawSourceBadgeText(m_meta.rawSource, m_meta.width, m_meta.height));
    }
    if (m_meta.frameCount > 1)
    {
        const bool pages = m_meta.sequenceKind == "pages";
        addLeaf(img, pages ? tr("Pages") : tr("Frames"), QString::number(m_meta.frameCount));
        addLeaf(img, pages ? tr("Current Page") : tr("Current Frame"),
                QString::number(m_meta.currentFrame + 1));
        if (!pages && m_meta.durationMs > 0)
            addLeaf(img, tr("Duration"), QString("%1 ms").arg(m_meta.durationMs));
        if (!pages && m_meta.loopCount >= 0)
            addLeaf(img, tr("Loop Count"), QString::number(m_meta.loopCount));
    }
    if (!m_meta.hasGps)
        return;
    auto toDms = [](double dd, char pos, char neg)
    {
        const bool isNeg = (dd < 0);
        const double d = std::abs(dd);
        const int deg = static_cast<int>(d);
        const double m = (d - deg) * 60.0;
        const int min = static_cast<int>(m);
        const double s = (m - min) * 60.0;
        return QString("%1°%2'%3\"%4")
            .arg(deg)
            .arg(min, 2, 10, QChar('0'))
            .arg(s, 2, 'f', 1)
            .arg(isNeg ? neg : pos);
    };
    addLeaf(img, tr("GPS Latitude"), toDms(m_meta.gpsLatitude, 'N', 'S'));
    addLeaf(img, tr("GPS Longitude"), toDms(m_meta.gpsLongitude, 'E', 'W'));
    if (m_meta.gpsAltitude != 0.0)
        addLeaf(img, tr("GPS Altitude"), QString("%1 m").arg(m_meta.gpsAltitude, 0, 'f', 1));
}

void MetadataModel::buildTextSection()
{
    if (m_meta.textKeys.empty())
        return;
    Node *exif = nullptr;
    for (const auto &kv : m_meta.textKeys)
    {
        if (kv.first.starts_with("MViewer."))
            continue;
        if (!exif)
            exif = addCategory(tr("EXIF / Metadata"));
        addLeaf(exif, toQString(kv.first), toQString(kv.second));
    }
}

void MetadataModel::buildIccSection()
{
    const bool hasIccDetails =
        m_meta.hasIccProfile && (!m_meta.iccDescription.empty() || !m_meta.iccCopyright.empty() ||
                                 !m_meta.iccColorSpace.empty() || !m_meta.iccDeviceClass.empty() ||
                                 !m_meta.iccPcs.empty() || !m_meta.iccRenderingIntent.empty() ||
                                 !m_meta.iccVersion.empty());
    if (!hasIccDetails)
        return;
    auto *icc = addCategory(tr("ICC Profile Details"));
    if (!m_meta.iccDescription.empty())
        addLeaf(icc, tr("Description"), toQString(m_meta.iccDescription));
    if (!m_meta.iccCopyright.empty())
        addLeaf(icc, tr("Copyright"), toQString(m_meta.iccCopyright));
    if (!m_meta.iccDeviceClass.empty())
        addLeaf(icc, tr("Device Class"), iccText(m_meta.iccDeviceClass));
    if (!m_meta.iccColorSpace.empty())
        addLeaf(icc, tr("Color Space"), iccText(m_meta.iccColorSpace));
    if (!m_meta.iccPcs.empty())
        addLeaf(icc, tr("PCS"), toQString(m_meta.iccPcs));
    if (!m_meta.iccRenderingIntent.empty())
        addLeaf(icc, tr("Rendering Intent"), iccText(m_meta.iccRenderingIntent));
    if (!m_meta.iccVersion.empty())
        addLeaf(icc, tr("Version"), toQString(m_meta.iccVersion));
}

void MetadataModel::buildRawSection()
{
    if (!m_raw.parsed)
        return;
    auto *raw = addCategory(tr("RAW Sensor"));
    if (!m_raw.make.empty())
        addLeaf(raw, tr("Camera Make"), toQString(m_raw.make));
    if (!m_raw.model.empty())
        addLeaf(raw, tr("Camera Model"), toQString(m_raw.model));
    if (!m_raw.lens.empty())
        addLeaf(raw, tr("Lens"), toQString(m_raw.lens));
    if (m_raw.iso > 0)
        addLeaf(raw, tr("ISO"), QString::number(m_raw.iso));
    if (m_raw.exposureSec > 0.0)
    {
        if (m_raw.exposureSec >= 1.0)
            addLeaf(raw, tr("Exposure Time"), QString("%1 s").arg(m_raw.exposureSec, 0, 'f', 2));
        else
            addLeaf(raw, tr("Exposure Time"),
                    QString("1/%1 s").arg(qRound(1.0 / m_raw.exposureSec)));
    }
    if (m_raw.fNumber > 0.0)
        addLeaf(raw, tr("Aperture"), QString("f/%1").arg(m_raw.fNumber, 0, 'f', 1));
    if (m_raw.focalLength > 0.0)
        addLeaf(raw, tr("Focal Length"), QString("%1 mm").arg(m_raw.focalLength, 0, 'f', 1));
    if (m_raw.focalLength35mm > 0.0)
        addLeaf(raw, tr("Equivalent Focal Length"),
                QString("%1 mm").arg(m_raw.focalLength35mm, 0, 'f', 1));
    if (!m_raw.bayerPattern.empty())
        addLeaf(raw, tr("Bayer Pattern"), toQString(m_raw.bayerPattern));
    if (m_raw.blackLevel > 0)
        addLeaf(raw, tr("Black Level"), QString::number(m_raw.blackLevel));
    if (m_raw.whiteLevel > 0)
        addLeaf(raw, tr("White Level"), QString::number(m_raw.whiteLevel));
    if (!m_raw.whiteBalance.empty())
        addLeaf(raw, tr("White Balance"), toQString(m_raw.whiteBalance));
    if (m_raw.bitsPerSample > 0)
        addLeaf(raw, tr("Bit Depth"), tr("%1-bit").arg(m_raw.bitsPerSample));
    if (m_raw.width > 0 && m_raw.height > 0)
        addLeaf(raw, tr("Original Size"), QString("%1 × %2").arg(m_raw.width).arg(m_raw.height));
    if (!m_raw.colorSpace.empty())
        addLeaf(raw, tr("Color Space"), toQString(m_raw.colorSpace));
}

QModelIndex MetadataModel::index(int row, int column, const QModelIndex &parent) const
{
    if (!hasIndex(row, column, parent))
        return {};
    Node *parentNode = parent.isValid() ? static_cast<Node *>(parent.internalPointer())
                                        : const_cast<Node *>(&m_root);
    if (row < 0 || row >= parentNode->children.size())
        return {};
    return createIndex(row, column, parentNode->children.at(row));
}

QModelIndex MetadataModel::parent(const QModelIndex &child) const
{
    if (!child.isValid())
        return {};
    auto *node = static_cast<Node *>(child.internalPointer());
    if (!node || node->parent == &m_root || !node->parent)
        return {};
    const int row = m_root.children.indexOf(node->parent);
    if (row < 0)
        return {};
    return createIndex(row, 0, node->parent);
}

int MetadataModel::rowCount(const QModelIndex &parent) const
{
    if (parent.column() > 0)
        return 0;
    Node *node = parent.isValid() ? static_cast<Node *>(parent.internalPointer())
                                  : const_cast<Node *>(&m_root);
    return node ? node->children.size() : 0;
}

int MetadataModel::columnCount(const QModelIndex &) const
{
    return 2;
}

QVariant MetadataModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || role != Qt::DisplayRole)
        return {};
    auto *node = static_cast<Node *>(index.internalPointer());
    if (!node)
        return {};
    if (index.column() == 0)
        return node->key;
    return node->value;
}

QVariant MetadataModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (orientation != Qt::Horizontal || role != Qt::DisplayRole)
        return {};
    return section == 0 ? tr("Field") : tr("Value");
}

Qt::ItemFlags MetadataModel::flags(const QModelIndex &index) const
{
    if (!index.isValid())
        return Qt::NoItemFlags;
    return Qt::ItemIsEnabled | Qt::ItemIsSelectable;
}
