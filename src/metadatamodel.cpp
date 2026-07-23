#include "metadatamodel.h"

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

QString orientationText(int o)
{
    switch (o)
    {
    case 1:
        return QString("正常 (1)");
    case 2:
        return QString("水平翻转 (2)");
    case 3:
        return QString("旋转 180° (3)");
    case 4:
        return QString("垂直翻转 (4)");
    case 5:
        return QString("转置 90° (5)");
    case 6:
        return QString("顺时针 90° (6)");
    case 7:
        return QString("横轴转置 90° (7)");
    case 8:
        return QString("逆时针 90° (8)");
    default:
        return QString("未知 (%1)").arg(o);
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
        return QString("未嵌入");
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
        auto *cat = addCategory(tr("提示"));
        addLeaf(cat, tr("说明"), tr("在画廊中选择一张图片以查看元数据"));
        endResetModel();
        return;
    }

    // ─── File section ───────────────────────────────────────────────────
    {
        auto *file = addCategory(tr("文件信息"));
        addLeaf(file, tr("文件名"), toQString(m_meta.fileName));
        addLeaf(file, tr("路径"), toQString(m_meta.filePath));
        addLeaf(file, tr("大小"),
                m_meta.fileSize > 0 ? QString("%1 字节").arg(m_meta.fileSize) : tr("未知"));
        if (m_meta.modifiedEpochSec > 0)
        {
            QDateTime t;
            t.setSecsSinceEpoch(static_cast<qint64>(m_meta.modifiedEpochSec));
            addLeaf(file, tr("修改时间"), t.toString("yyyy-MM-dd HH:mm:ss"));
        }
    }

    // ─── Image section ──────────────────────────────────────────────────
    {
        auto *img = addCategory(tr("图像信息"));
        addLeaf(img, tr("格式"), toQString(m_meta.format));
        if (m_meta.width > 0 && m_meta.height > 0)
        {
            const double mp = m_meta.width * m_meta.height / 1000000.0;
            addLeaf(img, tr("尺寸"), QString("%1 × %2 像素").arg(m_meta.width).arg(m_meta.height));
            addLeaf(img, tr("长宽比"), ratioText(m_meta.width, m_meta.height));
            addLeaf(img, tr("像素量"), QString("%1 MP").arg(mp, 0, 'f', 2));
        }
        addLeaf(img, tr("位深"),
                m_meta.bitDepth > 0 ? QString("%1 位").arg(m_meta.bitDepth) : tr("未知"));
        addLeaf(img, tr("通道数"),
                m_meta.channels > 0 ? QString::number(m_meta.channels) : tr("未知"));
        addLeaf(img, tr("色彩空间"), colorSpaceText(toQString(m_meta.colorSpace)));
        if (m_meta.dpiX > 0 || m_meta.dpiY > 0)
        {
            addLeaf(img, tr("分辨率 (DPI)"), QString("%1 × %2").arg(m_meta.dpiX).arg(m_meta.dpiY));
        }
        addLeaf(img, tr("方向"), orientationText(m_meta.orientation));
        addLeaf(img, tr("ICC 配置"), m_meta.hasIccProfile ? tr("已嵌入") : tr("无"));
    }

    // ─── EXIF / XMP / IPTC text keys ────────────────────────────────────
    if (!m_meta.textKeys.empty())
    {
        auto *exif = addCategory(tr("EXIF / 元数据"));
        for (const auto &kv : m_meta.textKeys)
        {
            addLeaf(exif, toQString(kv.first), toQString(kv.second));
        }
    }

    // ─── RAW sensor section ─────────────────────────────────────────────
    if (m_raw.parsed)
    {
        auto *raw = addCategory(tr("RAW 传感器"));
        if (!m_raw.make.empty())
            addLeaf(raw, tr("相机厂商"), toQString(m_raw.make));
        if (!m_raw.model.empty())
            addLeaf(raw, tr("相机型号"), toQString(m_raw.model));
        if (!m_raw.lens.empty())
            addLeaf(raw, tr("镜头"), toQString(m_raw.lens));
        if (m_raw.iso > 0)
            addLeaf(raw, tr("ISO"), QString::number(m_raw.iso));
        if (m_raw.exposureSec > 0.0)
        {
            if (m_raw.exposureSec >= 1.0)
                addLeaf(raw, tr("曝光时间"), QString("%1 s").arg(m_raw.exposureSec, 0, 'f', 2));
            else
                addLeaf(raw, tr("曝光时间"),
                        QString("1/%1 s").arg(qRound(1.0 / m_raw.exposureSec)));
        }
        if (m_raw.fNumber > 0.0)
            addLeaf(raw, tr("光圈"), QString("f/%1").arg(m_raw.fNumber, 0, 'f', 1));
        if (m_raw.focalLength > 0.0)
        {
            addLeaf(raw, tr("焦距"), QString("%1 mm").arg(m_raw.focalLength, 0, 'f', 1));
        }
        if (m_raw.focalLength35mm > 0.0)
        {
            addLeaf(raw, tr("等效焦距"), QString("%1 mm").arg(m_raw.focalLength35mm, 0, 'f', 1));
        }
        if (!m_raw.bayerPattern.empty())
            addLeaf(raw, tr("Bayer 阵列"), toQString(m_raw.bayerPattern));
        if (m_raw.blackLevel > 0)
            addLeaf(raw, tr("黑电平"), QString::number(m_raw.blackLevel));
        if (m_raw.whiteLevel > 0)
            addLeaf(raw, tr("白电平"), QString::number(m_raw.whiteLevel));
        if (!m_raw.whiteBalance.empty())
            addLeaf(raw, tr("白平衡"), toQString(m_raw.whiteBalance));
        if (m_raw.bitsPerSample > 0)
            addLeaf(raw, tr("采样位深"), QString("%1 位").arg(m_raw.bitsPerSample));
        if (m_raw.width > 0 && m_raw.height > 0)
        {
            addLeaf(raw, tr("原始尺寸"), QString("%1 × %2").arg(m_raw.width).arg(m_raw.height));
        }
        if (!m_raw.colorSpace.empty())
            addLeaf(raw, tr("色彩空间"), toQString(m_raw.colorSpace));
    }

    endResetModel();
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
    return section == 0 ? tr("字段") : tr("值");
}

Qt::ItemFlags MetadataModel::flags(const QModelIndex &index) const
{
    if (!index.isValid())
        return Qt::NoItemFlags;
    return Qt::ItemIsEnabled | Qt::ItemIsSelectable;
}
