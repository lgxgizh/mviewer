#pragma once

#include <QString>
#include <QStringList>
#include <QWidget>

#include "core/compare/Histogram.h"
#include "core/metadata/MetadataPresentationService.h"

class QLabel;
class HistogramWidget;
class QHideEvent;

/// M15 Product Shell P0: Semi-transparent metadata overlay that appears on top
/// of the ImageViewer showing key EXIF info (filename, dimensions, size, date,
/// camera, GPS, histogram). Toggled by 'I' key or click on image. Dismissed by
/// ESC.
///
/// Usage:
///   overlay->showForImage(path);  // reads metadata and shows
///   overlay->toggle();            // show/hide
class MetadataOverlay : public QWidget
{
    Q_OBJECT

  public:
    explicit MetadataOverlay(QWidget *parent = nullptr);

    /// Record the current image. Hidden overlays do no metadata work.
    void setImage(const QString &path);
    /// Show metadata for the given image path.
    void showForImage(const QString &path);
    /// Image path currently represented by the overlay request.
    QString currentImagePath() const { return m_requestedPath; }
    /// Toggle visibility.
    void toggle();
    /// Hide and clear.
    void hide();

    /// P0: Set a single histogram from an external source (ImageViewer).
    void setHistogram(const mviewer::core::Histogram &hist);

  signals:
    void visibilityChanged(bool visible);

  protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void hideEvent(QHideEvent *event) override;

  private:
    void requestMetadata();
    void buildContent(const mviewer::core::MetadataPresentationService::Snapshot &snapshot);
    void positionHistogram(const QRect &boxRect);

    QStringList m_lines;
    QString m_shortName;
    HistogramWidget *m_histogram = nullptr;
    QString m_requestedPath;
    uint64_t m_requestGeneration = 0;
    bool m_requestActive = false;
    std::string m_consumerId;

    // Auto-hide delay constants
    static constexpr int kInfoRectWidth = 380;
    static constexpr int kFontSize = 12;
    static constexpr int kHistogramHeight = 64;
};
