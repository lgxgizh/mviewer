#pragma once

#include "core/image/ImageBuffer.h"
#include "core/scheduler/TaskScheduler.h"
#include "domain/Image.h"
#include "widgets/rawimageview.h"

#include <QDialog>
#include <QImage>
#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QPushButton;
class QSlider;
class QSpinBox;
class QTimer;

namespace mviewer::core
{
struct UsmDiffSummary;
}

struct UsmLoadedSource
{
    ImageData pixels;
    mviewer::domain::ImageMetadata meta;
    std::string error;
    bool ok() const
    {
        return error.empty() && !pixels.isNull();
    }
};

class UsmCompareDialog : public QDialog
{
    Q_OBJECT

  public:
    enum class OpenMode : std::uint8_t
    {
        Immediate,
        Background
    };
    using SourceLoader = std::function<UsmLoadedSource(const std::string &path)>;
    using FailurePresenter = std::function<void(const QString &title, const QString &body)>;

    explicit UsmCompareDialog(const QString &filePath, const ImageData &initialData = {},
                              QWidget *parent = nullptr,
                              const mviewer::domain::ImageMetadata &meta = {},
                              OpenMode mode = OpenMode::Immediate, SourceLoader loader = {});
    ~UsmCompareDialog() override;

    static void setFailurePresenter(FailurePresenter presenter);
    bool loadedOffUiThread() const
    {
        return m_loadedOffUiThread;
    }

    const std::vector<uint8_t> &embeddedIccProfile() const
    {
        return m_iccProfile;
    }
    // RAW sources save as tif when the encoder maps it, otherwise png.
    // Other sources keep their suffix (empty becomes png).
    static QString defaultSaveExtension(const QString &sourceSuffix);

  protected:
    void keyPressEvent(QKeyEvent *event) override;
    void keyReleaseEvent(QKeyEvent *event) override;
    void showEvent(QShowEvent *event) override;
    void hideEvent(QHideEvent *event) override;
    void changeEvent(QEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;

  private slots:
    void onParametersChanged();
    void scheduleSharpen();
    void resetParameters();
    void saveSharpenedAs();
    void copySharpenedToClipboard();
    void onPresetSelected(int index);
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
    void updateStatusInfo(qint64 elapsedMs, const mviewer::core::UsmDiffSummary &summary);
    void loadImageSource(const QString &filePath, const ImageData &initialData,
                         const mviewer::domain::ImageMetadata &meta);
    void beginBackgroundLoad();
    void applyBackgroundLoad(const UsmLoadedSource &loaded, bool offUiThread);
    void presentOpenFailure(const QString &detail);

    // Momentary Space / hold-button compare (aligned with CompareWorkspace pair hold).
    int paneIndexAtGlobalPos(const QPoint &globalPos) const;
    void applyTemporaryDisplay(RawImageView *target, RawImageView *source);
    void beginTemporaryHold();
    void endTemporaryHold();

    QString m_filePath;
    ImageData m_sourceData;
    ImageData m_sharpenedData;
    mviewer::domain::ImageMetadata m_meta;
    std::vector<uint8_t> m_iccProfile;
    QImage m_origQImage;
    QImage m_sharpenedQImage;

    RawImageView *m_leftView = nullptr;
    RawImageView *m_rightView = nullptr;

    QPushButton *m_btnFit = nullptr;
    QPushButton *m_btn100 = nullptr;
    QPushButton *m_btn200 = nullptr;
    QCheckBox *m_syncCheck = nullptr;
    QPushButton *m_btnHoldCompare = nullptr;
    QPushButton *m_btnCopy = nullptr;
    QPushButton *m_btnSave = nullptr;

    QComboBox *m_comboPreset = nullptr;
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
    bool m_updatingPreset = false;
    bool m_temporaryHoldActive = false;
    RawImageView *m_temporaryTarget = nullptr;
    uint64_t m_currentGen = 0;
    std::shared_ptr<std::atomic<bool>> m_currentCancel;
    TaskScheduler::TaskHandle m_activeTask;
    SourceLoader m_sourceLoader;
    bool m_loadedOffUiThread = false;
    bool m_closing = false;
};
