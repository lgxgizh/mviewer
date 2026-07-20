#pragma once

#include <QDateTime>
#include <QMap>
#include <QTableWidget>
#include <QWidget>

#include "domain/Image.h"

// M18: Metadata panel — shows file-system + decode-time metadata for the
// currently selected image (filename, size, dimensions, format, bit depth,
// channels, color space, DPI, EXIF orientation, ICC profile, and any embedded
// EXIF/XMP text keys exposed by the Qt image plugin). Pure UI layer; reads via
// core::MetadataReader.
class MetadataPanel : public QWidget
{
    Q_OBJECT

  public:
    explicit MetadataPanel(QWidget *parent = nullptr);

  public slots:
    // Load + display metadata for the given image path.
    void setImage(const QString &path);
    // Clear the panel (e.g. when nothing is selected).
    void clear();

  private:
    void addRow(const QString &key, const QString &value);
    void render(const mviewer::domain::ImageMetadata &meta);

    QTableWidget *m_table = nullptr;
};
