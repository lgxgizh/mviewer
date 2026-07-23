#pragma once

#include <QAbstractItemModel>
#include <QVector>

#include "core/image/RawMetadata.h"
#include "domain/Image.h"

// M15 P0#4 Metadata Center: a single unified, hierarchical model that
// aggregates file-level ImageMetadata, embedded EXIF/XMP text keys, and RAW
// sensor metadata into grouped sections (File / Image / EXIF / RAW), driving a
// QTreeView. This is a pure Qt view-model (UI layer); it performs NO file I/O.
// Callers feed it already-read metadata (setImage / setRaw) so the model stays
// unit-testable without disk access and is reusable across panels.
class MetadataModel : public QAbstractItemModel
{
    Q_OBJECT

  public:
    explicit MetadataModel(QObject *parent = nullptr);
    ~MetadataModel() override;

    // Replace the model contents. Called independently: setImage() supplies the
    // file/image/EXIF sections; setRaw() supplies the RAW-sensor section.
    void setImage(const mviewer::domain::ImageMetadata &meta);
    void setRaw(const mviewer::core::RawMetadata &rm);
    void clear();

    // QAbstractItemModel interface
    QModelIndex index(int row, int column, const QModelIndex &parent = {}) const override;
    QModelIndex parent(const QModelIndex &child) const override;
    int rowCount(const QModelIndex &parent = {}) const override;
    int columnCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QVariant headerData(int section, Qt::Orientation orientation,
                        int role = Qt::DisplayRole) const override;
    Qt::ItemFlags flags(const QModelIndex &index) const override;

  private:
    struct Node
    {
        QString key;
        QString value;
        bool isCategory = false;
        Node *parent = nullptr;
        QVector<Node *> children;
        ~Node()
        {
            qDeleteAll(children);
        }
    };

    void clearNodes();
    void rebuild();
    Node *addCategory(const QString &title);
    void addLeaf(Node *cat, const QString &key, const QString &value);

    mviewer::domain::ImageMetadata m_meta;
    mviewer::core::RawMetadata m_raw;
    Node m_root;
};
