#pragma once

#include <QWidget>

#include "core/image/RawMetadata.h"
#include "core/image/ImageFrame.h"
#include "core/metadata/MetadataPresentationService.h"
#include "domain/Image.h"

class QTreeView;
class MetadataModel;
class RatingWidget;
class QComboBox;
class QPushButton;
class QHideEvent;

// M15 P0#4 Metadata Center: the metadata table is now rendered by a single
// unified MetadataModel + QTreeView. The rating / color-label / reject / pick
// controls (P1, P3) are preserved with their existing signals so MainWindow's
// gallery overlays keep refreshing.
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

  signals:
    // P1: emitted when the user changes the current image's star rating.
    void ratingEdited(const QString &path, int stars);
    // P3 tail: emitted when color label / reject / pick changes.
    void flagsEdited(const QString &path, int label, bool rejected, bool picked);

  private slots:
    void copyAll();
    void requestMetadata();

  private:
    void hideEvent(QHideEvent *event) override;
    QTreeView *m_tree = nullptr;
    MetadataModel *m_model = nullptr;
    RatingWidget *m_rating = nullptr;   // P1: 0-5 star editor
    QComboBox *m_colorLabel = nullptr;  // P3 tail: color label selector
    QPushButton *m_rejectBtn = nullptr; // P3 tail: reject toggle
    QPushButton *m_pickBtn = nullptr;   // P3 tail: pick/favorite toggle
    QString m_currentPath;              // P1: tracks the rated image
    uint64_t m_requestGeneration = 0;
    std::string m_consumerId;
};
