#pragma once

#include "core/image/ImageCache.h"
#include "core/image/ImageFrame.h"
#include "domain/CompareSession.h"

#include <string>
#include <vector>

// 二维整数点/尺寸与浮点向量：core 自有的 std 结构，替代 QPoint/QSize/QPointF。
// 接口层不依赖 Qt；UI 层在边界处自行与 QPoint/QSize/QPointF 互转。
struct CellPoint {
  int x = 0;
  int y = 0;
};

struct CellSize {
  int w = 0;
  int h = 0;
};

struct Vec2 {
  double x = 0.0;
  double y = 0.0;
};

// 比较模式：N 张图的网格布局规则
// 2=左右, 3=左中右, 4=2x2, 5-8=2行 x 4列
struct CompareLayout {
  int cols = 0;
  int rows = 0;
  int imageCount = 0;

  static CompareLayout forCount(int n);
  CellPoint cellPos(int index,
                    const CellSize &viewport) const; // 该图在视口内的偏移
  CellSize cellSize(const CellSize &viewport) const;
};

// 同步变换：所有比较图共享同一个 scale/offset，保证同步缩放/平移
struct SyncTransform {
  double scale = 1.0;
  Vec2 offset;
  bool enabled = true;
};

// Per-cell independent transform (when sync disabled)
struct CellTransform {
  double scale = 1.0;
  Vec2 offset;
};

enum class CompareState { Idle, Comparing, SyncZoom, SyncDrag };

// Compare Engine：持有 N 张图片，管理同步变换、闪烁、差异图。
// 完全独立于 QWidget/Qt，可由任意 Viewer/Workspace 使用。
class CompareEngine {
public:
  CompareEngine();

  // 图片管理
  void setImages(const std::vector<std::string> &paths);
  void addImage(const std::string &path);
  void removeImage(int index);
  void clear();
  // Session state (read-only view for UI; engine owns the live state)
  mviewer::domain::CompareSession session() const;

  int imageCount() const { return static_cast<int>(m_images.size()); }
  const ImageFrame &image(int index) const { return m_images[index]; }
  const ImageFrame *imageAt(int index) const;

  // 布局
  const CompareLayout &layout() const { return m_layout; }

  // 同步变换
  const SyncTransform &syncTransform() const { return m_sync; }
  void setSyncEnabled(bool on) { m_sync.enabled = on; }
  bool syncEnabled() const { return m_sync.enabled; }
  void setScale(double s);
  void setOffset(double ox, double oy);
  void zoomAt(double viewX, double viewY, double factor, int exceptIndex = -1);

  // Per-cell independent scale/offset (when sync off)
  double cellScale(int index) const;
  Vec2 cellOffset(int index) const;
  void setCellScale(int index, double s);
  void setCellOffset(int index, double ox, double oy);
  const CellTransform &cellTransform(int index) const;

  // Fit a cell to its viewport (centered, contain)
  void fitCell(int index, const CellSize &viewport, const CellSize &imageSize);

  // 闪烁比较：切换显示哪张(单张突出模式)
  int blinkIndex() const { return m_blinkIndex; }
  void setBlinkIndex(int idx);
  void clearBlink() { setBlinkIndex(-1); }

  // 差异图：返回 index 与 baseIndex 的像素差异灰度图(相同=黑,差异越大越亮)
  ImageData differenceMap(int index, int baseIndex = 0);

private:
  void rebuildLayout();

  std::vector<ImageFrame> m_images;
  CompareLayout m_layout;
  SyncTransform m_sync;
  std::vector<CellTransform> m_cells;
  int m_blinkIndex = -1;
};
