#pragma once

#include "core/scheduler/TaskScheduler.h"

#include <QPixmap>
#include <QSize>
#include <QString>
#include <QWidget>

#include <cstdint>
#include <string>

// Bottom-left panel: shows a single large preview of the currently
// selected image plus its filename and basic stats.
//
// The preview is served by a SINGLE scaled (<= kPreviewMaxEdge) decode on the
// Thumbnail pool — never a full ImageRepository::loadAsync / DecodePool decode
// (which duplicates ImageViewer work and retains a full QPixmap). The scaled
// result is cached in the existing CacheLevel::Preview layer (keyed by
// ImageRepository::makeKey(path) plus the max edge) and the UI thread only
// ever materializes a small QPixmap.
class PreviewPanel : public QWidget
{
    Q_OBJECT

  public:
    // Longest source edge kept for the preview pixmap. The displayed source
    // dimensions (sourceImageSize()) still reflect the original image.
    static constexpr int kPreviewMaxEdge = 512;

    explicit PreviewPanel(QWidget *parent = nullptr);
    ~PreviewPanel() override;

    enum class PresentationQuality
    {
        None,
        Thumbnail,
        Preview
    };

  public slots:
    void setImage(const QString &path, const QPixmap &warmThumbnail = QPixmap(),
                  const QSize &knownSourceSize = QSize(), qint64 knownFileSize = -1);

    // Test/embedding observability: whether a preview is currently shown.
    bool hasImage() const
    {
        return m_hasImage;
    }

    QString requestedPath() const
    {
        return m_requestedPath;
    }
    QString presentedPath() const
    {
        return m_presentedPath;
    }
    PresentationQuality presentationQuality() const
    {
        return m_quality;
    }

    // Original source dimensions (post-orientation), independent of the
    // preview cap.
    QSize sourceImageSize() const
    {
        return m_sourceDimensionsKnown ? QSize(m_imgW, m_imgH) : QSize();
    }

    // Pixel size of the preview pixmap actually held (max edge <= kPreviewMaxEdge).
    QSize previewPixelSize() const
    {
        return QSize(m_previewW, m_previewH);
    }

    // Preview-cache key: ImageRepository::makeKey(path) plus the preview max
    // edge, so a scaled preview is never confused with a FullImage decode.
    static std::string previewCacheKey(const std::string &path);

  protected:
    void paintEvent(QPaintEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;

  private:
    void cancelPending();
    void resetMatchingHandle(uint64_t gen);
    void rebuild();

    QString m_requestedPath;
    QString m_presentedPath;
    PresentationQuality m_quality = PresentationQuality::None;
    // Cancellable scaled-preview task (Thumbnail pool). UI-thread-owned; the
    // worker only observes its TaskContext cancel flag.
    TaskScheduler::TaskHandle m_task;
    QPixmap m_preview; // decoded preview (max edge <= kPreviewMaxEdge)
    QPixmap m_scaled;  // fitted preview
    int m_imgW = 0;    // source width (post-orientation)
    int m_imgH = 0;    // source height (post-orientation)
    int m_previewW = 0;
    int m_previewH = 0;
    qint64 m_fileSize = 0;
    bool m_sourceDimensionsKnown = false;
    bool m_fileSizeKnown = false;
    double m_lumMean = 0.0;
    int m_rMean = 0;
    int m_gMean = 0;
    int m_bMean = 0;
    bool m_hasImage = false;
    // M27: request generation — bumped on every setImage() so a late delivery
    // from an older request can never overwrite the current one, even for the
    // same path (e.g. A -> B -> A where the first A completes last).
    uint64_t m_requestGen = 0;
};
