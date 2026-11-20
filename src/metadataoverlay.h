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
/// The panel is sized from its content: the widest value sets the width (up to
/// a readable measure) and longer values wrap, so no line is ever clipped by
/// the box. `layout()` exposes the geometry so tests can assert that contract.
///
/// Usage:
///   overlay->showForImage(path);  // reads metadata and shows
///   overlay->toggle();            // show/hide
class MetadataOverlay : public QWidget
{
    Q_OBJECT

  public:
    struct Layout
    {
        QRect box;
        QRect title;
        QString titleText;
        QVector<QRect> lines;
        QStringList texts; // wrapped text drawn at each `lines` rect
        QRect histogram;   // empty when the histogram has no room / no data
        QRect hint;
    };

    explicit MetadataOverlay(QWidget *parent = nullptr);

    /// Record the current image. Hidden overlays do no metadata work.
    void setImage(const QString &path);
    /// Show metadata for the given image path.
    void showForImage(const QString &path);
    /// Image path currently represented by the overlay request.
    QString currentImagePath() const
    {
        return m_requestedPath;
    }
    /// Toggle visibility.
    void toggle();
    /// Hide and clear.
    void hide();

    /// P0: Set a single histogram from an external source (ImageViewer).
    void setHistogram(const mviewer::core::Histogram &hist);

    /// Geometry the next paint will use for a viewer of `viewerSize`.
    /// The box stays inside the viewer and every line stays inside the box.
    Layout layout(const QSize &viewerSize) const;

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
    void appendFileInfo(const mviewer::domain::ImageMetadata &meta);
    void appendExifInfo(const mviewer::domain::ImageMetadata &meta);
    void appendRawInfo(const mviewer::core::RawMetadata &raw);
    void appendLocationAndTimes(const mviewer::domain::ImageMetadata &meta);
    static QStringList wrapLine(const QFontMetrics &metrics, const QString &text, int width);
    // Append `line` only the first time `key` is seen. EXIF and RAW metadata
    // describe the same camera/lens/ISO fields; deduping on the rendered text
    // would break the moment the label is translated, so the field identity is
    // carried by `key` instead.
    void addField(const QString &key, const QString &line);

    QStringList m_lines;
    QStringList m_fields;
    QString m_shortName;
    HistogramWidget *m_histogram = nullptr;
    QString m_requestedPath;
    uint64_t m_requestGeneration = 0;
    bool m_requestActive = false;
    std::string m_consumerId;

    // Auto-hide delay constants
    static constexpr int kMinInfoWidth = 300;
    static constexpr int kMaxInfoWidth = 520;
    static constexpr int kInfoMargin = 20;
    static constexpr int kInfoPadding = 12;
    static constexpr int kTitleGap = 4;
    static constexpr int kHintGap = 6;
    static constexpr int kHintHeight = 18;
    static constexpr int kFontSize = 12;
    static constexpr int kTitleFontSize = 14;
    static constexpr int kInfoAlpha = 250; // reads as opaque over any image
    static constexpr int kHistogramHeight = 120;
    static constexpr int kMinHistogramHeight = 72;
    static constexpr int kHistogramGap = 8;
};
