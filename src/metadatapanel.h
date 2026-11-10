#pragma once

#include <QWidget>

#include "core/image/ImageFrame.h"
#include "core/image/RawMetadata.h"
#include "core/metadata/MetadataPresentationService.h"
#include "domain/Image.h"

class QTreeView;
class MetadataModel;
class QHideEvent;

// M15 P0#4 Metadata Center: the metadata table is now rendered by a single
// unified MetadataModel + QTreeView.
class MetadataPanel : public QWidget
{
    Q_OBJECT

  public:
    explicit MetadataPanel(QWidget *parent = nullptr);
    ~MetadataPanel() override;

  public slots:
    void setImage(const QString &path);
    void setFrame(const std::shared_ptr<ImageFrame> &frame);
    void clear();

  private slots:
    void copyAll();
    void requestMetadata();
    void onTreeContextMenu(const QPoint &pos);
    void onTreeDoubleClicked(const QModelIndex &index);

  private:
    void hideEvent(QHideEvent *event) override;
    QTreeView *m_tree = nullptr;
    MetadataModel *m_model = nullptr;
    QString m_currentPath;
    uint64_t m_requestGeneration = 0;
    std::string m_consumerId;
};
