#pragma once

#include "core/image/yuv/YuvExtensionMap.h"

#include <QString>
#include <QWidget>

#include <vector>

class QTableWidget;

// Preferences > YUV: editable "file extension -> pixel format" table backed by
// the core extension map (YuvExtensionMap) and QSettings "yuv/extensionMap".
// Pixel format "Auto-detect" only registers the extension; parameters then come
// from the file name, folder and size like a plain .yuv.
class YuvExtensionMapEditor : public QWidget
{
    Q_OBJECT
  public:
    explicit YuvExtensionMapEditor(QWidget *parent = nullptr);

    void setRules(const std::vector<mviewer::core::yuv::ExtensionRule> &rules);
    // Rows as typed; empty extensions are skipped. Call validationError() first.
    std::vector<mviewer::core::yuv::ExtensionRule> rules() const;
    // Empty when every row is usable; otherwise a translated reason.
    QString validationError() const;

    int rowCount() const;
    void addRow(const QString &ext, const QString &pixFmt);

  private:
    QTableWidget *m_table = nullptr;
};
