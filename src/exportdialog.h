#pragma once

#include <QDialog>

#include "core/export/ExportJob.h"
#include "core/image/ImageBuffer.h"

#include <QStringList>

#include <atomic>
#include <cstdint>
#include <memory>

class QLineEdit;
class QComboBox;
class QSpinBox;
class QCheckBox;
class QPushButton;
class QLabel;
class QProgressDialog;
class QCloseEvent;
class QVBoxLayout;

// Batch / single image export dialog. Supports format conversion, resizing,
// text watermarking, batch rename, contact-sheet generation and PDF export.
class ExportDialog : public QDialog
{
    Q_OBJECT

  public:
    explicit ExportDialog(QWidget *parent = nullptr);
    ~ExportDialog() override
    {
        if (m_cancelFlag)
            m_cancelFlag->store(true, std::memory_order_release);
    }
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
    void reject() override;

  protected:
    void closeEvent(QCloseEvent *event) override;

  private:
    QStringList collectSources() const;

    void buildOutputSection(QVBoxLayout *root);
    void buildModeSection(QVBoxLayout *root);
    void buildFormatSection(QVBoxLayout *root);
    void buildResizeSection(QVBoxLayout *root);
    void buildWatermarkSection(QVBoxLayout *root);
    void buildCropSection(QVBoxLayout *root);
    void buildMetadataSection(QVBoxLayout *root);
    void buildRenameSection(QVBoxLayout *root);
    void buildContactSection(QVBoxLayout *root);
    void buildButtonSection(QVBoxLayout *root);

    ImageData applyResize(const ImageData &d) const;
    ImageData applyWatermark(const ImageData &d) const;

    void exportConvertBatch();
    void exportContactSheet();
    void exportPdf();
    void exportCsv();
    void exportJson();
    void exportHtmlReport();
    void exportClipboard();
    void exportUnifiedMode(mviewer::exportjob::Mode mode);
    void cancelActiveExport();
    void startExportJob(mviewer::exportjob::ExportJobConfig cfg);

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

    // P0 #⑦: crop + strip metadata
    QCheckBox *m_cropCheck = nullptr;
    QSpinBox *m_cropX = nullptr;
    QSpinBox *m_cropY = nullptr;
    QSpinBox *m_cropW = nullptr;
    QSpinBox *m_cropH = nullptr;
    QCheckBox *m_stripMetaCheck = nullptr;

    QPushButton *m_exportBtn = nullptr;
    QLabel *m_statusLabel = nullptr;

    // M24 (D#3/D#7): async batch-convert — cancellation token, progress dialog,
    // and the dialog's UI-thread guard for the worker callback.
    std::shared_ptr<std::atomic<bool>> m_cancelFlag;
    QProgressDialog *m_progress = nullptr;
    uint64_t m_exportGeneration = 0;
};
