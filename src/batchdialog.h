#pragma once

#include "core/batch/BatchProcessor.h"
#include "domain/BatchJob.h"

#include <QDialog>
#include <memory>

class QListWidget;
class QCheckBox;
class QSpinBox;
class QDoubleSpinBox;
class QLineEdit;
class QComboBox;
class QPushButton;
class QProgressBar;
class QTextEdit;
class QLabel;

// BatchDialog is a modal dialog for configuring and running batch processing
// jobs. The user adds files, selects operations (resize, watermark, analyze,
// export, rename), sets parameters, and clicks "Start" to run the batch.
class BatchDialog : public QDialog
{
    Q_OBJECT
public:
    explicit BatchDialog(QWidget *parent = nullptr);

    // Pre-fill the file list with the given paths.
    void setInputFiles(const QStringList &paths);

private slots:
    void onAddFiles();
    void onRemoveSelected();
    void onStart();
    void onCancel();
    void onBrowseOutputDir();

private:
    void buildConfig(mviewer::domain::BatchJobConfig &config) const;
    void updateUiState(bool running);

    // ── file list ──────────────────────────────────────────────────
    QListWidget *m_fileList = nullptr;
    QPushButton *m_addBtn = nullptr;
    QPushButton *m_removeBtn = nullptr;

    // ── operations ─────────────────────────────────────────────────
    QCheckBox *m_chkAnalyze = nullptr;
    QCheckBox *m_chkResize = nullptr;
    QCheckBox *m_chkWatermark = nullptr;
    QCheckBox *m_chkRename = nullptr;
    QCheckBox *m_chkExport = nullptr;

    // ── resize params ──────────────────────────────────────────────
    QSpinBox *m_resizeMaxEdge = nullptr;

    // ── watermark params ───────────────────────────────────────────
    QLineEdit *m_watermarkText = nullptr;
    QComboBox *m_watermarkPos = nullptr;
    QDoubleSpinBox *m_watermarkOpacity = nullptr;
    QSpinBox *m_watermarkFontSize = nullptr;

    // ── rename params ──────────────────────────────────────────────
    QLineEdit *m_renamePattern = nullptr;

    // ── export params ──────────────────────────────────────────────
    QComboBox *m_exportFormat = nullptr;
    QSpinBox *m_exportQuality = nullptr;
    QLineEdit *m_outputDir = nullptr;
    QPushButton *m_browseBtn = nullptr;

    // ── progress ──────────────────────────────────────────────────
    QProgressBar *m_progress = nullptr;
    QTextEdit *m_log = nullptr;
    QLabel *m_statusLabel = nullptr;
    QPushButton *m_startBtn = nullptr;
    QPushButton *m_cancelBtn = nullptr;
    QPushButton *m_closeBtn = nullptr;

    std::unique_ptr<mviewer::core::BatchProcessor> m_processor;
};
