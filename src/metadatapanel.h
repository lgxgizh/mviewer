#pragma once

#include <QDateTime>
#include <QMap>
#include <QTableWidget>
#include <QWidget>

#include "domain/Image.h"

#include "core/image/RawMetadata.h"

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

  private:
    void addRow(const QString &key, const QString &value);
    void render(const mviewer::domain::ImageMetadata &meta);
    void renderRaw(const mviewer::core::RawMetadata &rm);

    QTableWidget *m_table = nullptr;
};
