#pragma once

#include "core/analysis/YuvMetrics.h"

#include <QDialog>
#include <QString>

#include <atomic>
#include <cstdint>
#include <mutex>
#include <thread>

class QLabel;
class QProgressBar;
class QPushButton;
class QTableWidget;
class QTimer;

class YuvCompareDialog : public QDialog
{
    Q_OBJECT

  public:
    YuvCompareDialog(const QString &pathA, const QString &pathB, QWidget *parent = nullptr);
    ~YuvCompareDialog() override;

    void start();
    bool isFinished() const;
    QString resultText() const;

    static QString frameRowText(int64_t index, const mviewer::core::yuv::YuvFrameMetrics &m);
    static QString psnrText(double db);

  private slots:
    void poll();

  private:
    void buildUi(const QString &pathA, const QString &pathB);
    void updateProgress(int64_t done, int64_t total);
    void populateResult();
    void fillTable(const mviewer::core::yuv::YuvSequenceMetrics &metrics);
    void showFailure(const mviewer::core::yuv::YuvSequenceMetrics &metrics);
    void showSuccess(const mviewer::core::yuv::YuvSequenceMetrics &metrics);

    QString m_nameA;
    QString m_nameB;
    QString m_pathA;
    QString m_pathB;
    QString m_resultText;
    mviewer::core::yuv::YuvSequenceMetrics m_metrics;
    std::mutex m_metricsMutex;
    std::thread m_worker;
    std::atomic<bool> m_cancel{false};
    std::atomic<bool> m_done{false};
    std::atomic<bool> m_started{false};
    std::atomic<int64_t> m_doneFrames{0};
    std::atomic<int64_t> m_totalFrames{0};
    bool m_finished = false;
    QLabel *m_paths = nullptr;
    QLabel *m_status = nullptr;
    QProgressBar *m_progress = nullptr;
    QTableWidget *m_table = nullptr;
    QPushButton *m_cancelButton = nullptr;
    QPushButton *m_copyButton = nullptr;
    QPushButton *m_closeButton = nullptr;
    QTimer *m_timer = nullptr;
};

// Shows the dialog (non-modal, WA_DeleteOnClose) and starts it. Returns the dialog.
YuvCompareDialog *runYuvCompare(QWidget *parent, const QString &pathA, const QString &pathB);
