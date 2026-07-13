#pragma once

#include "core/compare/CompareEngine.h"
#include "core/analysis/AnalysisEngine.h"

#include <QWidget>
#include <QLabel>
#include <QCheckBox>
#include <QGridLayout>
#include <QPixmap>
#include <QPointF>
#include <QHash>
#include <QMouseEvent>

// CompareWorkspace：多图同步比较工作区
class CompareWorkspace : public QWidget
{
    Q_OBJECT

public:
    explicit CompareWorkspace(QWidget *parent = nullptr);

    void setImages(const QStringList &paths);

    bool isSyncEnabled() const;
    void setSyncEnabled(bool on);

    CompareEngine &engine() { return m_engine; }

signals:
    void syncToggled(bool on);

protected:
    void paintEvent(QPaintEvent *) override;
    bool eventFilter(QObject *, QEvent *) override;
    void resizeEvent(QResizeEvent *) override;

private:
    void rebuildCells();
    void fitAll();
    void drawCellHistogram(QPainter &p, const QSize &cell, int index);

    CompareEngine m_engine;
    QCheckBox *m_syncZoomChk = nullptr;
    QCheckBox *m_syncDragChk = nullptr;
    bool m_syncZoom = true;
    bool m_syncDrag = true;
    QWidget *m_grid = nullptr;
    QGridLayout *m_layout = nullptr;
    QList<QLabel*> m_cells;
    QHash<int, ImageStats> m_stats;
    bool m_dragging = false;
    QPoint m_lastMouse;
    int m_dragIdx = -1;
};
