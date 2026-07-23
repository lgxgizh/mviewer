#pragma once

#include <QDialog>

#include "core/image/ImageBuffer.h"

#include <QStringList>

class QLineEdit;
class QComboBox;
class QSpinBox;
class QCheckBox;
class QPushButton;
class QLabel;

// Batch / single image export dialog. Supports format conversion, resizing,
// text watermarking, batch rename, contact-sheet generation and PDF export.
class ExportDialog : public QDialog
{
    Q_OBJECT

  public:
    explicit ExportDialog(QWidget *parent = nullptr);
    // Backward-compatible constructor: takes an explicit source list and
    // pre-selects the output directory to the first image's folder.
    explicit ExportDialog(const QStringList &sources, QWidget *parent = nullptr);

    // Legacy single-file export (used by the export command).
    void setPath(const QString &path)
    {
        m_path = path;
    }
    // Legacy batch output directory. When set (and no explicit sources are
    // provided) the dialog converts the images found in this directory.
    void setOutputDir(const QString &dir);

    // New: explicit list of source files to export (from the gallery selection).
    void setSources(const QStringList &paths)
    {
        m_sources = paths;
    }

  private slots:
    void onBrowse();
    void onExportClicked();

  private:
    QStringList collectSources() const;

    ImageData applyResize(const ImageData &d) const;
    ImageData applyWatermark(const ImageData &d) const;

    void exportConvertBatch();
    void exportContactSheet();
    void exportPdf();
    void exportCsv();
    void exportJson();
    void exportHtmlReport();
    void exportClipboard();

    QString m_path;
    QString m_outDir;
    QStringList m_sources;

    QLineEdit *m_dirEdit = nullptr;
    QPushButton *m_browseBtn = nullptr;
    QComboBox *m_formatCombo = nullptr;
    QSpinBox *m_qualitySpin = nullptr;
    QCheckBox *m_batchCheck = nullptr;

    QComboBox *m_modeCombo = nullptr;
    QComboBox *m_resizeCombo = nullptr;
    QSpinBox *m_resizeSpin = nullptr;
    QLineEdit *m_watermarkEdit = nullptr;
    QComboBox *m_wmPosCombo = nullptr;
    QSpinBox *m_wmOpacitySpin = nullptr;
    QLineEdit *m_renameEdit = nullptr;
    QSpinBox *m_colsSpin = nullptr;
    QSpinBox *m_thumbSpin = nullptr;

    QPushButton *m_exportBtn = nullptr;
    QLabel *m_statusLabel = nullptr;
};
