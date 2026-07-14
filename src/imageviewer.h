#pragma once

#include <QPixmap>
#include <QStringList>
#include <QWidget>

#include <list>
#include <unordered_map>

// Full-image zoomable viewer. Shown in its own window when the user
// double-clicks a thumbnail (or single-clicks the bottom-left preview).
// Supports wheel zoom, left-drag pan, brightness histogram overlay, and
// a LRU cache of decoded pixmaps.
class ImageViewer : public QWidget {
  Q_OBJECT

public:
  explicit ImageViewer(QWidget *parent = nullptr);
  ~ImageViewer() override;

  void setImage(const QString &path);

public slots:
  void setSelectMode(bool on);

signals:
  void regionStats(const QString &text);
  void selectionChanged(const QRect &sel); // image coords (may be null rect)
  void requestPrev();
  void requestNext();

protected:
  void paintEvent(QPaintEvent *event) override;
  void wheelEvent(QWheelEvent *event) override;
  void mousePressEvent(QMouseEvent *event) override;
  void mouseMoveEvent(QMouseEvent *event) override;
  void mouseReleaseEvent(QMouseEvent *event) override;
  void resizeEvent(QResizeEvent *event) override;
  void keyPressEvent(QKeyEvent *event) override;

private:
  void fitToWidget();
  void preloadNeighbors(const QString &path);
  void drawHistogram(QPainter &painter) const;
  void computeHistogram(const QPixmap &pixmap);

  QPixmap loadPixmap(const QString &path);

  static QStringList listImages(const QString &dirPath);

  static constexpr int kMaxCache = 5;

  QPixmap m_pixmap;
  QString m_currentPath;
  QStringList m_fileList;
  int m_currentIndex = -1;

  double m_scale = 1.0;
  QPointF m_offset;

  bool m_dragging = false;
  QPoint m_lastMousePos;

  std::list<QString> m_cacheOrder;
  std::unordered_map<QString, QPixmap> m_cache;

  int m_histogram[256] = {0};
  bool m_hasHistogram = false;

  bool m_selecting = false;
  bool m_selectMode = false;
  QPoint m_selStart, m_selEnd;

  QRect selectedRegion() const;
};
