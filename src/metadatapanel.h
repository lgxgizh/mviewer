#pragma once

#include <QDateTime>
#include <QMap>
#include <QTableWidget>
#include <QWidget>

#include "domain/Image.h"

#include "core/image/RawMetadata.h"

class RatingWidget;
class QComboBox;
class QPushButton;

// M18 + M14-2: Metadata panel — shows file-system + decode-time metadata AND
// RAW sensor metadata (ISO/exposure/focal/bayer) for RAW files.
class MetadataPanel : public QWidget
{
    Q_OBJECT

  public:
    explicit MetadataPanel(QWidget *parent = nullptr);

  public slots:
    void setImage(const QString &path);
    void clear();

  signals:
    // P1: emitted when the user changes the current image's star rating.
    void ratingEdited(const QString &path, int stars);
    // P3 tail: emitted when color label / reject / pick changes.
    void flagsEdited(const QString &path, int label, bool rejected, bool picked);

  private:
    void addRow(const QString &key, const QString &value);
    void render(const mviewer::domain::ImageMetadata &meta);
    void renderRaw(const mviewer::core::RawMetadata &rm);

    QTableWidget *m_table = nullptr;
    RatingWidget *m_rating = nullptr;  // P1: 0-5 star editor
    QComboBox *m_colorLabel = nullptr; // P3 tail: color label selector
    QPushButton *m_rejectBtn = nullptr; // P3 tail: reject toggle
    QPushButton *m_pickBtn = nullptr;    // P3 tail: pick/favorite toggle
    QString m_currentPath;             // P1: tracks the rated image
};
