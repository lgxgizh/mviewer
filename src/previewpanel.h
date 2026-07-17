#pragma once

#include <QPixmap>
#include <QString>
#include <QWidget>

// Bottom-left panel: shows a single large preview of the currently
// selected image plus its filename and basic stats.
class PreviewPanel : public QWidget
{
    Q_OBJECT

  public:
    explicit PreviewPanel(QWidget *parent = nullptr);

  public slots:
    void setImage(const QString &path);

  protected:
    void paintEvent(QPaintEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;

  private:
    void rebuild();
    void computeStats(const QPixmap &pm);

    QString m_path;
    QPixmap m_full;   // full image (for accurate stats)
    QPixmap m_scaled; // fitted preview
    int m_imgW = 0;
    int m_imgH = 0;
    qint64 m_fileSize = 0;
    double m_lumMean = 0.0;
    int m_rMean = 0;
    int m_gMean = 0;
    int m_bMean = 0;
    bool m_hasImage = false;
};
