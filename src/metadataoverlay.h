#pragma once

#include <QWidget>
#include <QString>
#include <QStringList>

class QLabel;

/// M15 Product Shell P0: Semi-transparent metadata overlay that appears on top
/// of the ImageViewer showing key EXIF info (filename, dimensions, size, date,
/// camera). Toggled by 'I' key or click on image. Dismissed by ESC.
///
/// Usage:
///   overlay->showForImage(path);  // reads metadata and shows
///   overlay->toggle();            // show/hide
class MetadataOverlay : public QWidget
{
    Q_OBJECT

  public:
    explicit MetadataOverlay(QWidget *parent = nullptr);

    /// Build metadata for the given image path without changing visibility.
    void setImage(const QString &path);
    /// Show metadata for the given image path.
    void showForImage(const QString &path);
    /// Toggle visibility.
    void toggle();
    /// Hide and clear.
    void hide();

  signals:
    void visibilityChanged(bool visible);

  protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;

  private:
    void buildContent(const QString &path);

    QStringList m_lines;
    QString m_shortName;

    // Auto-hide delay constants
    static constexpr int kInfoRectWidth = 380;
    static constexpr int kFontSize = 12;
};
