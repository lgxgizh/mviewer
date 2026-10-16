#pragma once

#include "core/image/ImageBuffer.h"
#include "core/scheduler/TaskScheduler.h"
#include "widgets/rawimageview.h"

#include <QDialog>
#include <QImage>
#include <atomic>
#include <cstdint>
#include <memory>

class QCheckBox;
class QDoubleSpinBox;
class QLabel;
class QPushButton;
class QSlider;
class QSpinBox;
class QTimer;

class UsmCompareDialog : public QDialog
{
    Q_OBJECT

  public:
    explicit UsmCompareDialog(const QString &filePath, const ImageData &initialData = {},
                              QWidget *parent = nullptr);
    ~UsmCompareDialog() override;

  protected:
    void keyPressEvent(QKeyEvent *event) override;
    void keyReleaseEvent(QKeyEvent *event) override;

  private slots:
    void onParametersChanged();
    void scheduleSharpen();
    void resetParameters();
    void saveSharpenedAs();
    void zoomFit();
    void zoom100();
    void zoom200();

  private:
    void setupUi();
    QWidget *createTopToolBar();
    QWidget *createCenterCanvas();
    QWidget *createBottomControls();
    QWidget *createStatusBarWidget();
    void setupConnections();
    void syncTransforms(RawImageView *source, RawImageView *target);
    void updateStatusInfo(qint64 elapsedMs, double psnr, double ssim);
    void loadImageSource(const QString &filePath, const ImageData &initialData);

    QString m_filePath;
    ImageData m_sourceData;
    ImageData m_sharpenedData;
    QImage m_origQImage;
    QImage m_sharpenedQImage;

    RawImageView *m_leftView = nullptr;
    RawImageView *m_rightView = nullptr;

    QPushButton *m_btnFit = nullptr;
    QPushButton *m_btn100 = nullptr;
    QPushButton *m_btn200 = nullptr;
    QCheckBox *m_syncCheck = nullptr;
    QPushButton *m_btnHoldCompare = nullptr;
    QPushButton *m_btnSave = nullptr;

    QSlider *m_sliderAmount = nullptr;
    QSpinBox *m_spinAmount = nullptr;
    QSlider *m_sliderRadius = nullptr;
    QDoubleSpinBox *m_spinRadius = nullptr;
    QSlider *m_sliderThreshold = nullptr;
    QSpinBox *m_spinThreshold = nullptr;
    QCheckBox *m_checkLumaOnly = nullptr;
    QPushButton *m_btnReset = nullptr;
    QPushButton *m_btnApply = nullptr;

    QLabel *m_lblImageInfo = nullptr;
    QLabel *m_lblCalcStatus = nullptr;
    QLabel *m_lblMetrics = nullptr;

    QTimer *m_debounceTimer = nullptr;
    bool m_syncing = false;
    uint64_t m_currentGen = 0;
    std::shared_ptr<std::atomic<bool>> m_currentCancel;
    TaskScheduler::TaskHandle m_activeTask;
};
